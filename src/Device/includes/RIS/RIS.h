/*
 * Copyright 2024 Tom Clabault. GNU GPL3 license.
 * GNU GPL3 license copy: https://www.gnu.org/licenses/gpl-3.0.txt
 */

#ifndef DEVICE_RIS_H
#define DEVICE_RIS_H

#include "Device/includes/Dispatcher.h"
#include "Device/includes/Intersect.h"
#include "Device/includes/LightUtils.h"
#include "Device/includes/RIS/RIS_Reservoir.h"

#include "HostDeviceCommon/Color.h"
#include "HostDeviceCommon/HitInfo.h"
#include "HostDeviceCommon/RenderData.h"

HIPRT_HOST_DEVICE HIPRT_INLINE ColorRGB32F evaluate_reservoir_sample(HIPRTRenderData& render_data, RayPayload& ray_payload, 
    const HitInfo& closest_hit_info, const float3& view_direction,
    const RISReservoir& reservoir, Xorshift32Generator& random_number_generator)
{
    ColorRGB32F final_color;

    if (reservoir.UCW <= 0.0f)
        // No valid sample means no light contribution
        return ColorRGB32F(0.0f);

    RISSample sample = reservoir.sample;

    bool in_shadow;
    float distance_to_light;
    float3 evaluated_point = closest_hit_info.inter_point + closest_hit_info.shading_normal * 1.0e-4f;
    float3 shadow_ray_direction = sample.point_on_light_source - evaluated_point;
    float3 shadow_ray_direction_normalized = shadow_ray_direction / (distance_to_light = hippt::length(shadow_ray_direction));

    NEEPlusPlusContext nee_plus_plus_context;
    if (sample.is_bsdf_sample)
        // A BSDF sample that has been picked by RIS cannot be occluded otherwise
        // it would have a weight of 0 and would never be picked by RIS
        in_shadow = false;
    else
    {
        hiprtRay shadow_ray;
        shadow_ray.origin = evaluated_point;
        shadow_ray.direction = shadow_ray_direction_normalized;

        nee_plus_plus_context.point_on_light = sample.point_on_light_source;
        nee_plus_plus_context.shaded_point = shadow_ray.origin;
        in_shadow = evaluate_shadow_ray_nee_plus_plus(render_data, shadow_ray, distance_to_light, closest_hit_info.primitive_index, nee_plus_plus_context, random_number_generator, ray_payload.bounce);
    }

    if (!in_shadow)
    {
        float bsdf_pdf;
        float cosine_at_evaluated_point;
        ColorRGB32F bsdf_color;

        if (sample.is_bsdf_sample)
        {
            // If we picked a BSDF sample, we're using the already computed cosine term and color
            // because it's annoying to recompute it (we have to know if the BSDF is a refraction
            // sample or not)
            bsdf_color = sample.bsdf_sample_contribution;
            cosine_at_evaluated_point = sample.bsdf_sample_cosine_term;
        }
        else
        {
            bsdf_color = bsdf_dispatcher_eval(render_data, ray_payload.material, ray_payload.volume_state, false, 
                                              view_direction, closest_hit_info.shading_normal, closest_hit_info.geometric_normal, shadow_ray_direction_normalized, 
                                              bsdf_pdf, random_number_generator, ray_payload.bounce);

            cosine_at_evaluated_point = hippt::max(0.0f, hippt::dot(closest_hit_info.shading_normal, shadow_ray_direction_normalized));
        }

        if (cosine_at_evaluated_point > 0.0f)
        {
            int material_index = render_data.buffers.material_indices[sample.emissive_triangle_index];
            ColorRGB32F sample_emission = render_data.buffers.materials_buffer.get_emission(material_index);

            final_color = bsdf_color * reservoir.UCW * sample_emission * cosine_at_evaluated_point;
            if (!sample.is_bsdf_sample)
                final_color /= nee_plus_plus_context.unoccluded_probability;
        }
    }

    return final_color;
}

HIPRT_HOST_DEVICE HIPRT_INLINE RISReservoir sample_bsdf_and_lights_RIS_reservoir(const HIPRTRenderData& render_data, RayPayload& ray_payload, const HitInfo closest_hit_info, const float3& view_direction, Xorshift32Generator& random_number_generator, MISBSDFRayReuse& mis_ray_reuse)
{
    // Pushing the intersection point outside the surface (if we're already outside)
    // or inside the surface (if we're inside the surface)
    // We'll use that intersection point as the origin of our shadow rays
    bool inside_surface = hippt::dot(view_direction, closest_hit_info.geometric_normal) < 0;
    float inside_surface_multiplier = inside_surface ? -1.0f : 1.0f;

    // If we're rendering at low resolution, only doing 1 candidate of each
    // for better interactive framerates
    int nb_light_candidates = render_data.render_settings.do_render_low_resolution() ? 1 : render_data.render_settings.ris_settings.number_of_light_candidates;
    int nb_bsdf_candidates = render_data.render_settings.do_render_low_resolution() ? 1 : render_data.render_settings.ris_settings.number_of_bsdf_candidates;

    if (!MaterialUtils::can_do_light_sampling(ray_payload.material))
        nb_light_candidates = 0;

    // Sampling candidates with weighted reservoir sampling
    RISReservoir reservoir;
    for (int i = 0; i < nb_light_candidates; i++)
    {
        float light_sample_pdf;
        float distance_to_light;
        float cosine_at_light_source;
        float cosine_at_evaluated_point;
        LightSourceInformation light_source_info;

        ColorRGB32F bsdf_color;
        float target_function = 0.0f;
        float candidate_weight = 0.0f;
        float3 random_light_point = uniform_sample_one_emissive_triangle(render_data, random_number_generator, light_sample_pdf, light_source_info);

        if (light_sample_pdf > 0.0f)
        {
            // It can happen that the light PDF returned by the emissive triangle
            // sampling function is 0 because of emissive triangles that are so
            // small that we cannot compute their normal and their area (the cross
            // product of their edges gives a quasi-null vector --> length of 0.0f --> area of 0)

            float3 to_light_direction;
            to_light_direction = random_light_point - closest_hit_info.inter_point;
            distance_to_light = hippt::length(to_light_direction);
            to_light_direction = to_light_direction / distance_to_light; // Normalization
            cosine_at_light_source = hippt::abs(hippt::dot(light_source_info.light_source_normal, -to_light_direction));
            // Multiplying by the inside_surface_multiplier here because if we're inside the surface, we want to flip the normal
            // for the dot product to be "properly" oriented.
            cosine_at_evaluated_point = hippt::max(0.0f, hippt::dot(closest_hit_info.shading_normal * inside_surface_multiplier, to_light_direction));
            if (cosine_at_evaluated_point > 0.0f && cosine_at_light_source > 1.0e-6f)
            {
                // Converting the PDF from area measure to solid angle measure requires dividing by
                // cos(theta) / dist^2. Dividing by that factor is equal to multiplying by the inverse
                // which is what we're doing here
                light_sample_pdf *= distance_to_light * distance_to_light;
                light_sample_pdf /= cosine_at_light_source;

                float bsdf_pdf = 0.0f;
                // Early check for minimum light contribution: if the light itself doesn't contribute enough,
                // adding the BSDF attenuation on top of it will only make it worse so we can already
                // skip the light and saves ourselves the evaluation of the BSDF
                bool contributes_enough = check_minimum_light_contribution(render_data.render_settings.minimum_light_contribution, light_source_info.emission / light_sample_pdf);
                if (!contributes_enough)
                    target_function = 0.0f;
                else
                {
                    // Only going to evaluate the target function if we passed the preliminary minimum light contribution test

                    bsdf_color = bsdf_dispatcher_eval(render_data, ray_payload.material, ray_payload.volume_state, false, 
                                                      view_direction, closest_hit_info.shading_normal, closest_hit_info.geometric_normal, to_light_direction, 
                                                      bsdf_pdf, random_number_generator, ray_payload.bounce);

                    ColorRGB32F light_contribution = bsdf_color * light_source_info.emission * cosine_at_evaluated_point;
                    // Checking the light contribution and taking the BSDF and light PDFs into account
                    contributes_enough = check_minimum_light_contribution(render_data.render_settings.minimum_light_contribution, light_contribution / bsdf_pdf / light_sample_pdf);
                    if (!contributes_enough)
                        // The light doesn't contribute enough, setting the target function to 0.0f
                        // so that this light sample is skipped
                        // 
                        // Also, if at least one thread is going to evaluate the light anyways, because of the divergence that this would
                        // create, we may as well evaluate the light for all threads and not loose that much performance anyways
                        target_function = 0.0f;
                    else
                        target_function = light_contribution.luminance();
                }

#if RISUseVisiblityTargetFunction == KERNEL_OPTION_TRUE
                if (!render_data.render_settings.do_render_low_resolution() && target_function > 0.0f)
                {
                    // Only doing visiblity if we're not rendering at low resolution
                    // (meaning we're moving the camera) for better interaction framerates

                    hiprtRay shadow_ray;
                    shadow_ray.origin = closest_hit_info.inter_point;
                    shadow_ray.direction = to_light_direction;

                    bool visible = !evaluate_shadow_ray(render_data, shadow_ray, distance_to_light, closest_hit_info.primitive_index, ray_payload.bounce, random_number_generator);

                    target_function *= visible;
                }
#endif

                float mis_weight = balance_heuristic(light_sample_pdf, nb_light_candidates, bsdf_pdf, nb_bsdf_candidates);
                candidate_weight = mis_weight * target_function / light_sample_pdf;
            }
        }

        RISSample light_RIS_sample;
        light_RIS_sample.is_bsdf_sample = false;
        light_RIS_sample.point_on_light_source = random_light_point;
        light_RIS_sample.target_function = target_function;
        light_RIS_sample.emissive_triangle_index = light_source_info.emissive_triangle_index;

        reservoir.add_one_candidate(light_RIS_sample, candidate_weight, random_number_generator);
        reservoir.sanity_check();
    }

    // Whether or not a BSDF sample has been retained by the reservoir
    for (int i = 0; i < nb_bsdf_candidates; i++)
    {
        float bsdf_sample_pdf = 0.0f;
        float target_function = 0.0f;
        float candidate_weight = 0.0f;
        float3 sampled_bsdf_direction;
        ColorRGB32F bsdf_color;

        bsdf_color = bsdf_dispatcher_sample(render_data, ray_payload.material, ray_payload.volume_state, true, 
                                            view_direction, closest_hit_info.shading_normal, closest_hit_info.geometric_normal, sampled_bsdf_direction, 
                                            bsdf_sample_pdf, random_number_generator, ray_payload.bounce);

        bool hit_found = false;
        float cosine_at_evaluated_point = 0.0f;
        RISSample bsdf_RIS_sample;
        ShadowLightRayHitInfo shadow_light_ray_hit_info;
        if (bsdf_sample_pdf > 0.0f)
        {
            hiprtRay bsdf_ray;
            bsdf_ray.origin = closest_hit_info.inter_point;
            bsdf_ray.direction = sampled_bsdf_direction;

            hit_found = evaluate_shadow_light_ray(render_data, bsdf_ray, 1.0e35f, shadow_light_ray_hit_info, closest_hit_info.primitive_index, ray_payload.bounce, random_number_generator);
            if (hit_found && !shadow_light_ray_hit_info.hit_emission.is_black())
            {
                // If we intersected an emissive material, compute the weight. 
                // Otherwise, the weight is 0 because of the emision being 0 so we just don't compute it

                // Using abs here because we want the dot product to be positive.
                // You may be thinking that if we're doing this, then we're not going to discard BSDF
                // sampled direction that are below the surface (whereas we should discard them).
                // That would be correct but bsdf_dispatcher_sample return a PDF == 0.0f if a bad
                // direction was sampled and if the PDF is 0.0f, we never get to this line of code
                // you're reading. If we are here, this is because we sampled a direction that is
                // correct for the BSDF. Even if the direction is correct, the dot product may be
                // negative in the case of refractions / total internal reflections and so in this case,
                // we'll need to abs() the dot product for it to be positive
                cosine_at_evaluated_point = hippt::abs(hippt::dot(closest_hit_info.shading_normal, sampled_bsdf_direction));

                // Our target function does not include the geometry term because we're integrating
                // in solid angle. The geometry term in the target function ( / in the integrand) is only
                // for surface area direct lighting integration
                ColorRGB32F light_contribution = bsdf_color * shadow_light_ray_hit_info.hit_emission * cosine_at_evaluated_point;
                target_function = light_contribution.luminance();

                float light_pdf = pdf_of_emissive_triangle_hit(render_data, shadow_light_ray_hit_info, sampled_bsdf_direction);
                // If we refracting, drop the light PDF to 0
                // 
                // Why?
                // 
                // Because right now, we allow sampling BSDF refractions. This means that we can sample a light
                // that is inside an object with a BSDF sample. However, a light sample to the same light cannot
                // be sampled because there's is going to be the surface of the object we're currently on in-between.
                // Basically, we are not allowing light sample refractions and so they should have a weight of 0 which
                // is what we're doing here: the pdf of a light sample that refracts through a surface is 0.
                //
                // If not doing that, we're going to have bad MIS weights that don't sum up to 1
                // (because the BSDF sample, that should have weight 1 [or to be precise: 1 / nb_bsdf_samples]
                // will have weight 1 / (1 + nb_light_samples) [or to be precise: 1 / (nb_bsdf_samples + nb_light_samples)]
                // and this is going to cause darkening as the number of light samples grows)
                bool refraction_sampled = hippt::dot(sampled_bsdf_direction, closest_hit_info.shading_normal * inside_surface_multiplier) < 0;
                light_pdf *= !refraction_sampled;

                bool contributes_enough = check_minimum_light_contribution(render_data.render_settings.minimum_light_contribution, light_contribution / light_pdf / bsdf_sample_pdf);
                if (!contributes_enough)
                    target_function = 0.0f;

                float mis_weight = balance_heuristic(bsdf_sample_pdf, nb_bsdf_candidates, light_pdf, nb_light_candidates);
                candidate_weight = mis_weight * target_function / bsdf_sample_pdf;

                bsdf_RIS_sample.emissive_triangle_index = shadow_light_ray_hit_info.hit_prim_index;
                bsdf_RIS_sample.point_on_light_source = bsdf_ray.origin + bsdf_ray.direction * shadow_light_ray_hit_info.hit_distance;
                bsdf_RIS_sample.is_bsdf_sample = true;
                bsdf_RIS_sample.bsdf_sample_contribution = bsdf_color;
                bsdf_RIS_sample.bsdf_sample_cosine_term = cosine_at_evaluated_point;
                bsdf_RIS_sample.target_function = target_function;
            }
        }

        // Fill the MIS BSDF ray reuse structure
        // 
        // Note that the structure is also filled even if the BSDF sample is incorrect i.e. the BSDF sampled 
        // a * reflection * below the surface
        // 
        // But an incorrect BSDF (sampled a reflection that goes below the surface for example)
        // sample should also be considered otherwise this is biased.
        // 
        // This is biased because if we do not indicate anything about the MIS BSDF sample, then
        // the main path tracing loop is going to assume that there is no BSDF MIS ray to
        // reuse and so it's going to sample the BSDF for a bounce direction. But that's where the bias is.
        // By doing this (re-sampling the BSDF again because the first sample we got from MIS was incorrect),
        // we're eseentially doing rejection sampling on the BSDF. If the BSDF has a GGX lobe 
        // (which it very much likely has) then we're doing rejection sampling on the GGX distribution. 
        // We're rejecting samples from the GGX that are below the surface. That's biased. 
        // Rejection sampling on the GGX distribution cannot be naively done:
        // 
        // See this a derivation on why this is biased (leads to energy gains): 
        // https://computergraphics.stackexchange.com/questions/14123/lots-of-bad-samples-below-the-hemisphere-when-sampling-the-ggx-vndf
        float3 bsdf_ray_inter_point = closest_hit_info.inter_point + shadow_light_ray_hit_info.hit_distance * sampled_bsdf_direction;
        mis_ray_reuse.fill(shadow_light_ray_hit_info, bsdf_ray_inter_point, sampled_bsdf_direction, bsdf_color, bsdf_sample_pdf,
            hit_found ? RayState::BOUNCE : RayState::MISSED);


        reservoir.add_one_candidate(bsdf_RIS_sample, candidate_weight, random_number_generator);
        reservoir.sanity_check();
    }

    reservoir.end();
    return reservoir;
}

HIPRT_HOST_DEVICE HIPRT_INLINE ColorRGB32F sample_lights_RIS(HIPRTRenderData& render_data, RayPayload& ray_payload, const HitInfo closest_hit_info, const float3& view_direction, Xorshift32Generator& random_number_generator, MISBSDFRayReuse& mis_ray_reuse)
{
    if (render_data.buffers.emissive_triangles_count == 0)
        return ColorRGB32F(0.0f);

    RISReservoir reservoir = sample_bsdf_and_lights_RIS_reservoir(render_data, ray_payload, closest_hit_info, view_direction, random_number_generator, mis_ray_reuse);

    return evaluate_reservoir_sample(render_data, ray_payload, 
        closest_hit_info, view_direction, 
        reservoir, random_number_generator);
}

#endif
