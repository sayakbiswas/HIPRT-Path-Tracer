/*
 * Copyright 2024 Tom Clabault. GNU GPL3 license.
 * GNU GPL3 license copy: https://www.gnu.org/licenses/gpl-3.0.txt
 */

#include "Renderer/GPURenderer.h"
#include "Renderer/RenderPasses/ReSTIRDIRenderPass.h"
#include "Threads/ThreadFunctions.h"
#include "Threads/ThreadManager.h"

const std::string ReSTIRDIRenderPass::RESTIR_DI_INITIAL_CANDIDATES_KERNEL_ID = "ReSTIR DI Initial Candidates";
const std::string ReSTIRDIRenderPass::RESTIR_DI_TEMPORAL_REUSE_KERNEL_ID = "ReSTIR DI Temporal Reuse";
const std::string ReSTIRDIRenderPass::RESTIR_DI_SPATIAL_REUSE_KERNEL_ID = "ReSTIR DI Spatial Reuse";
const std::string ReSTIRDIRenderPass::RESTIR_DI_SPATIOTEMPORAL_REUSE_KERNEL_ID = "ReSTIR DI Spatiotemporal Reuse";
const std::string ReSTIRDIRenderPass::RESTIR_DI_LIGHTS_PRESAMPLING_KERNEL_ID = "ReSTIR DI Lights Presampling";

const std::unordered_map<std::string, std::string> ReSTIRDIRenderPass::KERNEL_FUNCTION_NAMES =
{
	{ RESTIR_DI_INITIAL_CANDIDATES_KERNEL_ID, "ReSTIR_DI_InitialCandidates" },
	{ RESTIR_DI_TEMPORAL_REUSE_KERNEL_ID, "ReSTIR_DI_TemporalReuse" },
	{ RESTIR_DI_SPATIAL_REUSE_KERNEL_ID, "ReSTIR_DI_SpatialReuse" },
	{ RESTIR_DI_SPATIOTEMPORAL_REUSE_KERNEL_ID, "ReSTIR_DI_SpatiotemporalReuse" },
	{ RESTIR_DI_LIGHTS_PRESAMPLING_KERNEL_ID, "ReSTIR_DI_LightsPresampling" },
};

const std::unordered_map<std::string, std::string> ReSTIRDIRenderPass::KERNEL_FILES =
{
	{ RESTIR_DI_INITIAL_CANDIDATES_KERNEL_ID, DEVICE_KERNELS_DIRECTORY "/ReSTIR/DI/InitialCandidates.h" },
	{ RESTIR_DI_TEMPORAL_REUSE_KERNEL_ID, DEVICE_KERNELS_DIRECTORY "/ReSTIR/DI/TemporalReuse.h" },
	{ RESTIR_DI_SPATIAL_REUSE_KERNEL_ID, DEVICE_KERNELS_DIRECTORY "/ReSTIR/DI/SpatialReuse.h" },
	{ RESTIR_DI_SPATIOTEMPORAL_REUSE_KERNEL_ID, DEVICE_KERNELS_DIRECTORY "/ReSTIR/DI/FusedSpatiotemporalReuse.h" },
	{ RESTIR_DI_LIGHTS_PRESAMPLING_KERNEL_ID, DEVICE_KERNELS_DIRECTORY "/ReSTIR/DI/LightsPresampling.h" },
};

ReSTIRDIRenderPass::ReSTIRDIRenderPass(GPURenderer* renderer) : m_renderer(renderer), render_data(&renderer->get_render_data())
{
	OROCHI_CHECK_ERROR(oroEventCreate(&spatial_reuse_time_start));
	OROCHI_CHECK_ERROR(oroEventCreate(&spatial_reuse_time_stop));

	std::shared_ptr<GPUKernelCompilerOptions> global_compiler_options = m_renderer->get_global_compiler_options();

	m_kernels[ReSTIRDIRenderPass::RESTIR_DI_INITIAL_CANDIDATES_KERNEL_ID].set_kernel_file_path(ReSTIRDIRenderPass::KERNEL_FILES.at(ReSTIRDIRenderPass::RESTIR_DI_INITIAL_CANDIDATES_KERNEL_ID));
	m_kernels[ReSTIRDIRenderPass::RESTIR_DI_INITIAL_CANDIDATES_KERNEL_ID].set_kernel_function_name(ReSTIRDIRenderPass::KERNEL_FUNCTION_NAMES.at(ReSTIRDIRenderPass::RESTIR_DI_INITIAL_CANDIDATES_KERNEL_ID));
	m_kernels[ReSTIRDIRenderPass::RESTIR_DI_INITIAL_CANDIDATES_KERNEL_ID].synchronize_options_with(*global_compiler_options, GPURenderer::GPURenderer::KERNEL_OPTIONS_NOT_SYNCHRONIZED);
	m_kernels[ReSTIRDIRenderPass::RESTIR_DI_INITIAL_CANDIDATES_KERNEL_ID].get_kernel_options().set_macro_value(GPUKernelCompilerOptions::USE_SHARED_STACK_BVH_TRAVERSAL, KERNEL_OPTION_TRUE);
	m_kernels[ReSTIRDIRenderPass::RESTIR_DI_INITIAL_CANDIDATES_KERNEL_ID].get_kernel_options().set_macro_value(GPUKernelCompilerOptions::SHARED_STACK_BVH_TRAVERSAL_SIZE, 16);

	m_kernels[ReSTIRDIRenderPass::RESTIR_DI_TEMPORAL_REUSE_KERNEL_ID].set_kernel_file_path(ReSTIRDIRenderPass::KERNEL_FILES.at(ReSTIRDIRenderPass::RESTIR_DI_TEMPORAL_REUSE_KERNEL_ID));
	m_kernels[ReSTIRDIRenderPass::RESTIR_DI_TEMPORAL_REUSE_KERNEL_ID].set_kernel_function_name(ReSTIRDIRenderPass::KERNEL_FUNCTION_NAMES.at(ReSTIRDIRenderPass::RESTIR_DI_TEMPORAL_REUSE_KERNEL_ID));
	m_kernels[ReSTIRDIRenderPass::RESTIR_DI_TEMPORAL_REUSE_KERNEL_ID].synchronize_options_with(*global_compiler_options, GPURenderer::GPURenderer::KERNEL_OPTIONS_NOT_SYNCHRONIZED);
	m_kernels[ReSTIRDIRenderPass::RESTIR_DI_TEMPORAL_REUSE_KERNEL_ID].get_kernel_options().set_macro_value(GPUKernelCompilerOptions::USE_SHARED_STACK_BVH_TRAVERSAL, KERNEL_OPTION_TRUE);
	m_kernels[ReSTIRDIRenderPass::RESTIR_DI_TEMPORAL_REUSE_KERNEL_ID].get_kernel_options().set_macro_value(GPUKernelCompilerOptions::SHARED_STACK_BVH_TRAVERSAL_SIZE, 16);

	m_kernels[ReSTIRDIRenderPass::RESTIR_DI_SPATIAL_REUSE_KERNEL_ID].set_kernel_file_path(ReSTIRDIRenderPass::KERNEL_FILES.at(ReSTIRDIRenderPass::RESTIR_DI_SPATIAL_REUSE_KERNEL_ID));
	m_kernels[ReSTIRDIRenderPass::RESTIR_DI_SPATIAL_REUSE_KERNEL_ID].set_kernel_function_name(ReSTIRDIRenderPass::KERNEL_FUNCTION_NAMES.at(ReSTIRDIRenderPass::RESTIR_DI_SPATIAL_REUSE_KERNEL_ID));
	m_kernels[ReSTIRDIRenderPass::RESTIR_DI_SPATIAL_REUSE_KERNEL_ID].synchronize_options_with(*global_compiler_options, GPURenderer::GPURenderer::KERNEL_OPTIONS_NOT_SYNCHRONIZED);
	m_kernels[ReSTIRDIRenderPass::RESTIR_DI_SPATIAL_REUSE_KERNEL_ID].get_kernel_options().set_macro_value(GPUKernelCompilerOptions::USE_SHARED_STACK_BVH_TRAVERSAL, KERNEL_OPTION_TRUE);
	m_kernels[ReSTIRDIRenderPass::RESTIR_DI_SPATIAL_REUSE_KERNEL_ID].get_kernel_options().set_macro_value(GPUKernelCompilerOptions::SHARED_STACK_BVH_TRAVERSAL_SIZE, 8);

	m_kernels[ReSTIRDIRenderPass::RESTIR_DI_SPATIOTEMPORAL_REUSE_KERNEL_ID].set_kernel_file_path(ReSTIRDIRenderPass::KERNEL_FILES.at(ReSTIRDIRenderPass::RESTIR_DI_SPATIOTEMPORAL_REUSE_KERNEL_ID));
	m_kernels[ReSTIRDIRenderPass::RESTIR_DI_SPATIOTEMPORAL_REUSE_KERNEL_ID].set_kernel_function_name(ReSTIRDIRenderPass::KERNEL_FUNCTION_NAMES.at(ReSTIRDIRenderPass::RESTIR_DI_SPATIOTEMPORAL_REUSE_KERNEL_ID));
	m_kernels[ReSTIRDIRenderPass::RESTIR_DI_SPATIOTEMPORAL_REUSE_KERNEL_ID].synchronize_options_with(*global_compiler_options, GPURenderer::GPURenderer::KERNEL_OPTIONS_NOT_SYNCHRONIZED);
	m_kernels[ReSTIRDIRenderPass::RESTIR_DI_SPATIOTEMPORAL_REUSE_KERNEL_ID].get_kernel_options().set_macro_value(GPUKernelCompilerOptions::USE_SHARED_STACK_BVH_TRAVERSAL, KERNEL_OPTION_TRUE);
	m_kernels[ReSTIRDIRenderPass::RESTIR_DI_SPATIOTEMPORAL_REUSE_KERNEL_ID].get_kernel_options().set_macro_value(GPUKernelCompilerOptions::SHARED_STACK_BVH_TRAVERSAL_SIZE, 24);

	m_kernels[ReSTIRDIRenderPass::RESTIR_DI_LIGHTS_PRESAMPLING_KERNEL_ID].set_kernel_file_path(ReSTIRDIRenderPass::KERNEL_FILES.at(ReSTIRDIRenderPass::RESTIR_DI_LIGHTS_PRESAMPLING_KERNEL_ID));
	m_kernels[ReSTIRDIRenderPass::RESTIR_DI_LIGHTS_PRESAMPLING_KERNEL_ID].set_kernel_function_name(ReSTIRDIRenderPass::KERNEL_FUNCTION_NAMES.at(ReSTIRDIRenderPass::RESTIR_DI_LIGHTS_PRESAMPLING_KERNEL_ID));
	m_kernels[ReSTIRDIRenderPass::RESTIR_DI_LIGHTS_PRESAMPLING_KERNEL_ID].synchronize_options_with(*global_compiler_options, GPURenderer::GPURenderer::KERNEL_OPTIONS_NOT_SYNCHRONIZED);
	m_kernels[ReSTIRDIRenderPass::RESTIR_DI_LIGHTS_PRESAMPLING_KERNEL_ID].get_kernel_options().set_macro_value(GPUKernelCompilerOptions::USE_SHARED_STACK_BVH_TRAVERSAL, KERNEL_OPTION_TRUE);
	m_kernels[ReSTIRDIRenderPass::RESTIR_DI_LIGHTS_PRESAMPLING_KERNEL_ID].get_kernel_options().set_macro_value(GPUKernelCompilerOptions::SHARED_STACK_BVH_TRAVERSAL_SIZE, 0);
}

void ReSTIRDIRenderPass::compile(std::shared_ptr<HIPRTOrochiCtx> hiprt_orochi_ctx, std::vector<hiprtFuncNameSet>& func_name_sets)
{
	ThreadManager::start_thread(ThreadManager::COMPILE_KERNELS_THREAD_KEY, ThreadFunctions::compile_kernel, std::ref(m_kernels[ReSTIRDIRenderPass::RESTIR_DI_INITIAL_CANDIDATES_KERNEL_ID]), hiprt_orochi_ctx, std::ref(func_name_sets));
	ThreadManager::start_thread(ThreadManager::COMPILE_KERNELS_THREAD_KEY, ThreadFunctions::compile_kernel, std::ref(m_kernels[ReSTIRDIRenderPass::RESTIR_DI_TEMPORAL_REUSE_KERNEL_ID]), hiprt_orochi_ctx, std::ref(func_name_sets));
	ThreadManager::start_thread(ThreadManager::COMPILE_KERNELS_THREAD_KEY, ThreadFunctions::compile_kernel, std::ref(m_kernels[ReSTIRDIRenderPass::RESTIR_DI_SPATIAL_REUSE_KERNEL_ID]), hiprt_orochi_ctx, std::ref(func_name_sets));
	ThreadManager::start_thread(ThreadManager::COMPILE_KERNELS_THREAD_KEY, ThreadFunctions::compile_kernel, std::ref(m_kernels[ReSTIRDIRenderPass::RESTIR_DI_SPATIOTEMPORAL_REUSE_KERNEL_ID]), hiprt_orochi_ctx, std::ref(func_name_sets));
	ThreadManager::start_thread(ThreadManager::COMPILE_KERNELS_THREAD_KEY, ThreadFunctions::compile_kernel, std::ref(m_kernels[ReSTIRDIRenderPass::RESTIR_DI_LIGHTS_PRESAMPLING_KERNEL_ID]), hiprt_orochi_ctx, std::ref(func_name_sets));
}

void ReSTIRDIRenderPass::recompile(std::shared_ptr<HIPRTOrochiCtx>& hiprt_orochi_ctx, const std::vector<hiprtFuncNameSet>& func_name_sets, bool silent, bool use_cache)
{
	for (auto& name_to_kernel : m_kernels)
	{
		if (silent)
			name_to_kernel.second.compile_silent(hiprt_orochi_ctx, func_name_sets, use_cache);
		else
			name_to_kernel.second.compile(hiprt_orochi_ctx, func_name_sets, use_cache);
	}
}

void ReSTIRDIRenderPass::precompile_kernels(GPUKernelCompilerOptions partial_options, std::shared_ptr<HIPRTOrochiCtx> hiprt_orochi_ctx, const std::vector<hiprtFuncNameSet>& func_name_sets)
{
	GPUKernelCompilerOptions options;

	options = m_kernels[ReSTIRDIRenderPass::RESTIR_DI_LIGHTS_PRESAMPLING_KERNEL_ID].get_kernel_options().deep_copy();
	partial_options.apply_onto(options);
	ThreadManager::start_thread(ThreadManager::RESTIR_DI_PRECOMPILE_KERNELS, ThreadFunctions::precompile_kernel,
		ReSTIRDIRenderPass::KERNEL_FUNCTION_NAMES.at(ReSTIRDIRenderPass::RESTIR_DI_LIGHTS_PRESAMPLING_KERNEL_ID),
		ReSTIRDIRenderPass::KERNEL_FILES.at(ReSTIRDIRenderPass::RESTIR_DI_LIGHTS_PRESAMPLING_KERNEL_ID),
		options, hiprt_orochi_ctx, std::ref(func_name_sets));

	options = m_kernels[ReSTIRDIRenderPass::RESTIR_DI_INITIAL_CANDIDATES_KERNEL_ID].get_kernel_options().deep_copy();
	partial_options.apply_onto(options);
	ThreadManager::start_thread(ThreadManager::RESTIR_DI_PRECOMPILE_KERNELS, ThreadFunctions::precompile_kernel,
		ReSTIRDIRenderPass::KERNEL_FUNCTION_NAMES.at(ReSTIRDIRenderPass::RESTIR_DI_INITIAL_CANDIDATES_KERNEL_ID),
		ReSTIRDIRenderPass::KERNEL_FILES.at(ReSTIRDIRenderPass::RESTIR_DI_INITIAL_CANDIDATES_KERNEL_ID),
		options, hiprt_orochi_ctx, std::ref(func_name_sets));

	options = m_kernels[ReSTIRDIRenderPass::RESTIR_DI_SPATIAL_REUSE_KERNEL_ID].get_kernel_options().deep_copy();
	partial_options.apply_onto(options);
	ThreadManager::start_thread(ThreadManager::RESTIR_DI_PRECOMPILE_KERNELS, ThreadFunctions::precompile_kernel,
		ReSTIRDIRenderPass::KERNEL_FUNCTION_NAMES.at(ReSTIRDIRenderPass::RESTIR_DI_SPATIAL_REUSE_KERNEL_ID),
		ReSTIRDIRenderPass::KERNEL_FILES.at(ReSTIRDIRenderPass::RESTIR_DI_SPATIAL_REUSE_KERNEL_ID),
		options, hiprt_orochi_ctx, std::ref(func_name_sets));

	options = m_kernels[ReSTIRDIRenderPass::RESTIR_DI_TEMPORAL_REUSE_KERNEL_ID].get_kernel_options().deep_copy();
	partial_options.apply_onto(options);
	ThreadManager::start_thread(ThreadManager::RESTIR_DI_PRECOMPILE_KERNELS, ThreadFunctions::precompile_kernel,
		ReSTIRDIRenderPass::KERNEL_FUNCTION_NAMES.at(ReSTIRDIRenderPass::RESTIR_DI_TEMPORAL_REUSE_KERNEL_ID),
		ReSTIRDIRenderPass::KERNEL_FILES.at(ReSTIRDIRenderPass::RESTIR_DI_TEMPORAL_REUSE_KERNEL_ID),
		options, hiprt_orochi_ctx, std::ref(func_name_sets));

	options = m_kernels[ReSTIRDIRenderPass::RESTIR_DI_SPATIOTEMPORAL_REUSE_KERNEL_ID].get_kernel_options().deep_copy();
	partial_options.apply_onto(options);
	ThreadManager::start_thread(ThreadManager::RESTIR_DI_PRECOMPILE_KERNELS, ThreadFunctions::precompile_kernel,
		ReSTIRDIRenderPass::KERNEL_FUNCTION_NAMES.at(ReSTIRDIRenderPass::RESTIR_DI_SPATIOTEMPORAL_REUSE_KERNEL_ID),
		ReSTIRDIRenderPass::KERNEL_FILES.at(ReSTIRDIRenderPass::RESTIR_DI_SPATIOTEMPORAL_REUSE_KERNEL_ID),
		options, hiprt_orochi_ctx, std::ref(func_name_sets));

	ThreadManager::detach_threads(ThreadManager::RESTIR_DI_PRECOMPILE_KERNELS);
}

void ReSTIRDIRenderPass::pre_render_update()
{
	int2 render_resolution = m_renderer->m_render_resolution;

	if (m_renderer->get_global_compiler_options()->get_macro_value(GPUKernelCompilerOptions::DIRECT_LIGHT_SAMPLING_STRATEGY) == LSS_RESTIR_DI)
	{
		// ReSTIR DI enabled
		bool initial_candidates_reservoir_needs_resize = initial_candidates_reservoirs.get_element_count() == 0;
		bool spatial_output_1_needs_resize = spatial_output_reservoirs_1.get_element_count() == 0;
		bool spatial_output_2_needs_resize = spatial_output_reservoirs_2.get_element_count() == 0;

		if (initial_candidates_reservoir_needs_resize || spatial_output_1_needs_resize || spatial_output_2_needs_resize)
			// At least on buffer is going to be resized so buffers are invalidated
			m_renderer->invalidate_render_data_buffers();

		if (initial_candidates_reservoir_needs_resize)
			initial_candidates_reservoirs.resize(render_resolution.x * render_resolution.y);

		if (spatial_output_1_needs_resize)
			spatial_output_reservoirs_1.resize(render_resolution.x * render_resolution.y);

		if (spatial_output_2_needs_resize)
			spatial_output_reservoirs_2.resize(render_resolution.x * render_resolution.y);



		// Also allocating / deallocating the presampled lights buffer
		if (m_renderer->get_global_compiler_options()->get_macro_value(GPUKernelCompilerOptions::RESTIR_DI_DO_LIGHTS_PRESAMPLING) == KERNEL_OPTION_TRUE)
		{
			ReSTIRDISettings& restir_di_settings = m_renderer->get_render_settings().restir_di_settings;
			int presampled_light_count = restir_di_settings.light_presampling.number_of_subsets * restir_di_settings.light_presampling.subset_size;
			bool presampled_lights_needs_allocation = presampled_lights_buffer.get_element_count() != presampled_light_count;

			if (presampled_lights_needs_allocation)
			{
				presampled_lights_buffer.resize(presampled_light_count);

				// At least on buffer is going to be resized so buffers are invalidated
				m_renderer->invalidate_render_data_buffers();
			}
		}
		else
			presampled_lights_buffer.free();
	}
	else
	{
		// ReSTIR DI disabled, we're going to free the buffers if that's not already done
		if (initial_candidates_reservoirs.get_element_count() > 0 || spatial_output_reservoirs_1.get_element_count() > 0 || spatial_output_reservoirs_2.get_element_count() > 0)
		{
			initial_candidates_reservoirs.free();
			spatial_output_reservoirs_1.free();
			spatial_output_reservoirs_2.free();

			m_renderer->invalidate_render_data_buffers();
		}
	}
}

void ReSTIRDIRenderPass::update_render_data()
{
	// Setting the pointers for use in reset_render() in the camera rays kernel
	if (m_renderer->get_global_compiler_options()->get_macro_value(GPUKernelCompilerOptions::DIRECT_LIGHT_SAMPLING_STRATEGY) == LSS_RESTIR_DI)
	{
		render_data->aux_buffers.restir_reservoir_buffer_1 = initial_candidates_reservoirs.get_device_pointer();
		render_data->aux_buffers.restir_reservoir_buffer_2 = spatial_output_reservoirs_1.get_device_pointer();
		render_data->aux_buffers.restir_reservoir_buffer_3 = spatial_output_reservoirs_2.get_device_pointer();

		// If we just got ReSTIR enabled back, setting this one arbitrarily and resetting its content
		std::vector<ReSTIRDIReservoir> empty_reservoirs(m_renderer->m_render_resolution.x * m_renderer->m_render_resolution.y, ReSTIRDIReservoir());
		render_data->render_settings.restir_di_settings.restir_output_reservoirs = spatial_output_reservoirs_1.get_device_pointer();
		spatial_output_reservoirs_1.upload_data(empty_reservoirs);
	}
	else
	{
		// If ReSTIR DI is disabled, setting the pointers to nullptr so that the camera rays kernel
		// for example can detect that the buffers are freed and doesn't try to reset them or do
		// anything with them (which would lead to a crash since we would be accessing nullptr buffers)

		render_data->aux_buffers.restir_reservoir_buffer_1 = nullptr;
		render_data->aux_buffers.restir_reservoir_buffer_2 = nullptr;
		render_data->aux_buffers.restir_reservoir_buffer_3 = nullptr;
	}
}

void ReSTIRDIRenderPass::resize(int new_width, int new_height)
{
	initial_candidates_reservoirs.resize(new_width * new_height);
	spatial_output_reservoirs_2.resize(new_width * new_height);
	spatial_output_reservoirs_1.resize(new_width * new_height);
}

void ReSTIRDIRenderPass::reset()
{
	odd_frame = false;
}

void ReSTIRDIRenderPass::launch()
{
	ReSTIRDISettings& restir_di_settings = m_renderer->get_render_data().render_settings.restir_di_settings;

	if (m_renderer->get_global_compiler_options()->get_macro_value(GPUKernelCompilerOptions::DIRECT_LIGHT_SAMPLING_STRATEGY) == LSS_RESTIR_DI)
	{
		// If ReSTIR DI is enabled

		if (m_renderer->get_global_compiler_options()->get_macro_value(GPUKernelCompilerOptions::RESTIR_DI_DO_LIGHTS_PRESAMPLING) == KERNEL_OPTION_TRUE)
			launch_presampling_lights_pass();

		launch_initial_candidates_pass();

		if (render_data->render_settings.restir_di_settings.do_fused_spatiotemporal)
			// Launching the fused spatiotemporal kernel
			launch_spatiotemporal_pass();
		else
		{
			// Launching the temporal and spatial passes separately

			if (restir_di_settings.temporal_pass.do_temporal_reuse_pass)
				launch_temporal_reuse_pass();

			if (restir_di_settings.spatial_pass.do_spatial_reuse_pass)
				launch_spatial_reuse_passes();
		}

		configure_output_buffer();

		odd_frame = !odd_frame;
	}
}

LightPresamplingParameters ReSTIRDIRenderPass::configure_light_presampling_pass()
{
	LightPresamplingParameters parameters;
	/**
	 * Parameters specific to the kernel
	 */

	 // From all the lights of the scene, how many subsets to presample
	parameters.number_of_subsets = render_data->render_settings.restir_di_settings.light_presampling.number_of_subsets;
	// How many lights to presample in each subset
	parameters.subset_size = render_data->render_settings.restir_di_settings.light_presampling.subset_size;
	// Buffer that holds the presampled lights
	parameters.out_light_samples = presampled_lights_buffer.get_device_pointer();




	/**
	 * Generic parameters needed by the kernel
	 */
	parameters.emissive_triangles_count = render_data->buffers.emissive_triangles_count;
	parameters.emissive_triangles_indices = render_data->buffers.emissive_triangles_indices;
	parameters.triangles_indices = render_data->buffers.triangles_indices;
	parameters.vertices_positions = render_data->buffers.vertices_positions;
	parameters.material_indices = render_data->buffers.material_indices;
	parameters.materials = render_data->buffers.materials_buffer;

	// World settings for sampling the envmap
	parameters.world_settings = render_data->world_settings;

	parameters.freeze_random = render_data->render_settings.freeze_random;
	parameters.sample_number = render_data->render_settings.sample_number;
	parameters.random_seed = m_renderer->rng().xorshift32();

	// For each presampled light, the probability that this is going to be an envmap sample
	parameters.envmap_sampling_probability = render_data->render_settings.restir_di_settings.initial_candidates.envmap_candidate_probability;

	return parameters;
}

void ReSTIRDIRenderPass::launch_presampling_lights_pass()
{
	LightPresamplingParameters launch_parameters = configure_light_presampling_pass();

	void* launch_args[] = { &launch_parameters };
	int thread_count = render_data->render_settings.restir_di_settings.light_presampling.number_of_subsets * render_data->render_settings.restir_di_settings.light_presampling.subset_size;

	m_kernels[ReSTIRDIRenderPass::RESTIR_DI_LIGHTS_PRESAMPLING_KERNEL_ID].launch_asynchronous(32, 1, thread_count, 1, launch_args, m_renderer->get_main_stream());
}

void ReSTIRDIRenderPass::configure_initial_pass()
{
	render_data->random_seed = m_renderer->rng().xorshift32();
	render_data->render_settings.restir_di_settings.light_presampling.light_samples = presampled_lights_buffer.get_device_pointer();
	render_data->render_settings.restir_di_settings.initial_candidates.output_reservoirs = initial_candidates_reservoirs.get_device_pointer();
}

void ReSTIRDIRenderPass::launch_initial_candidates_pass()
{
	int2 render_resolution = m_renderer->m_render_resolution;
	void* launch_args[] = { &m_renderer->get_render_data(), &m_renderer->m_render_resolution };

	configure_initial_pass();
	m_kernels[ReSTIRDIRenderPass::RESTIR_DI_INITIAL_CANDIDATES_KERNEL_ID].launch_asynchronous(KernelBlockWidthHeight, KernelBlockWidthHeight, render_resolution.x, render_resolution.y, launch_args, m_renderer->get_main_stream());
}

void ReSTIRDIRenderPass::configure_temporal_pass()
{
	render_data->random_seed = m_renderer->rng().xorshift32();
	render_data->render_settings.restir_di_settings.temporal_pass.permutation_sampling_random_bits = m_renderer->rng().xorshift32();

	// The input of the temporal pass is the output of last frame's
	// ReSTIR (and also the initial candidates but this is implicit
	// and hardcoded in the shader)
	render_data->render_settings.restir_di_settings.temporal_pass.input_reservoirs = render_data->render_settings.restir_di_settings.restir_output_reservoirs;

	if (render_data->render_settings.restir_di_settings.spatial_pass.do_spatial_reuse_pass)
		// If we're going to do spatial reuse, reuse the initial
		// candidate reservoirs to store the output of the temporal pass.
		// The spatial reuse pass will read form that buffer.
		// 
		// Reusing the initial candidates buffer (which is an input
		// to the temporal pass) as the output is legal and does not
		// cause a race condition because a given pixel only read and
		// writes to its own pixel in the initial candidates buffer.
		// We're not risking another pixel reading in someone else's
		// pixel in the initial candidates buffer while we write into
		// it (that would be a race condition)
		render_data->render_settings.restir_di_settings.temporal_pass.output_reservoirs = initial_candidates_reservoirs.get_device_pointer();
	else
	{
		// Else, no spatial reuse, the output of the temporal pass is going to be in its own buffer (because otherwise, 
		// if we output in the initial candidates buffer, then it's going to be overriden by the initial candidates pass of the next frame).
		// Alternatively using spatial_output_reservoirs_1 and spatial_output_reservoirs_2 to avoid race conditions
		if (odd_frame)
			render_data->render_settings.restir_di_settings.temporal_pass.output_reservoirs = spatial_output_reservoirs_1.get_device_pointer();
		else
			render_data->render_settings.restir_di_settings.temporal_pass.output_reservoirs = spatial_output_reservoirs_2.get_device_pointer();
	}
}

void ReSTIRDIRenderPass::launch_temporal_reuse_pass()
{
	int2 render_resolution = m_renderer->m_render_resolution;
	void* launch_args[] = { &m_renderer->get_render_data(), &render_resolution };

	configure_temporal_pass();
	m_kernels[ReSTIRDIRenderPass::RESTIR_DI_TEMPORAL_REUSE_KERNEL_ID].launch_asynchronous(KernelBlockWidthHeight, KernelBlockWidthHeight, render_resolution.x, render_resolution.y, launch_args, m_renderer->get_main_stream());
}

void ReSTIRDIRenderPass::configure_temporal_pass_for_fused_spatiotemporal()
{
	render_data->random_seed = m_renderer->rng().xorshift32();
	render_data->render_settings.restir_di_settings.temporal_pass.permutation_sampling_random_bits = m_renderer->rng().xorshift32();

	// The input of the temporal pass is the output of last frame's
	// ReSTIR (and also the initial candidates but this is implicit
	// and hardcoded in the shader)
	render_data->render_settings.restir_di_settings.temporal_pass.input_reservoirs = render_data->render_settings.restir_di_settings.restir_output_reservoirs;

	// Not needed. In the fused spatiotemporal pass, everything is output by the spatial pass
	render_data->render_settings.restir_di_settings.temporal_pass.output_reservoirs = nullptr;
}

void ReSTIRDIRenderPass::configure_spatial_pass(int spatial_pass_index)
{
	render_data->random_seed = m_renderer->rng().xorshift32();
	render_data->render_settings.restir_di_settings.spatial_pass.spatial_pass_index = spatial_pass_index;

	if (spatial_pass_index == 0)
	{
		if (render_data->render_settings.restir_di_settings.temporal_pass.do_temporal_reuse_pass)
			// For the first spatial reuse pass, we hardcode reading from the output of the temporal pass and storing into 'spatial_output_reservoirs_1'
			render_data->render_settings.restir_di_settings.spatial_pass.input_reservoirs = render_data->render_settings.restir_di_settings.temporal_pass.output_reservoirs;
		else
			// If there is no temporal reuse pass, using the initial candidates as the input to the spatial reuse pass
			render_data->render_settings.restir_di_settings.spatial_pass.input_reservoirs = render_data->render_settings.restir_di_settings.initial_candidates.output_reservoirs;

		render_data->render_settings.restir_di_settings.spatial_pass.output_reservoirs = spatial_output_reservoirs_1.get_device_pointer();
	}
	else
	{
		// And then, starting at the second spatial reuse pass, we read from the output of the previous spatial pass and store
		// in either spatial_output_reservoirs_1 or spatial_output_reservoirs_2, depending on which one isn't the input (we don't
		// want to store in the same buffers that is used for output because that's a race condition so
		// we're ping-ponging between the two outputs of the spatial reuse pass)

		if ((spatial_pass_index & 1) == 0)
		{
			render_data->render_settings.restir_di_settings.spatial_pass.input_reservoirs = spatial_output_reservoirs_2.get_device_pointer();
			render_data->render_settings.restir_di_settings.spatial_pass.output_reservoirs = spatial_output_reservoirs_1.get_device_pointer();
		}
		else
		{
			render_data->render_settings.restir_di_settings.spatial_pass.input_reservoirs = spatial_output_reservoirs_1.get_device_pointer();
			render_data->render_settings.restir_di_settings.spatial_pass.output_reservoirs = spatial_output_reservoirs_2.get_device_pointer();

		}
	}
}

void ReSTIRDIRenderPass::configure_spatial_pass_for_fused_spatiotemporal(int spatial_pass_index)
{
	ReSTIRDISettings& restir_settings = render_data->render_settings.restir_di_settings;
	restir_settings.spatial_pass.spatial_pass_index = spatial_pass_index;
	render_data->random_seed = m_renderer->rng().xorshift32();

	if (spatial_pass_index == 0)
	{
		// The input of the spatial resampling in the fused spatiotemporal pass is the
		// temporal buffer of the last frame i.e. the input to the temporal pass
		//
		// Note, this line of code below assumes that the temporal pass was configured
		// prior to calling this function such that
		// 'restir_settings.temporal_pass.input_reservoirs'
		// is the proper pointer
		restir_settings.spatial_pass.input_reservoirs = restir_settings.temporal_pass.input_reservoirs;
	}
	else
	{
		// If this is not the first spatial reuse pass, the input is the output of the previous pass
		restir_settings.spatial_pass.input_reservoirs = restir_settings.spatial_pass.output_reservoirs;
	}

	// Outputting in whichever isn't the input
	if (restir_settings.spatial_pass.input_reservoirs == spatial_output_reservoirs_1.get_device_pointer())
		restir_settings.spatial_pass.output_reservoirs = spatial_output_reservoirs_2.get_device_pointer();
	else
		restir_settings.spatial_pass.output_reservoirs = spatial_output_reservoirs_1.get_device_pointer();
}

void ReSTIRDIRenderPass::launch_spatial_reuse_passes()
{
	int2 render_resolution = m_renderer->m_render_resolution;
	void* launch_args[] = { &m_renderer->get_render_data(), &render_resolution};

	// Emitting an event for timing all the spatial reuse passes combined
	OROCHI_CHECK_ERROR(oroEventRecord(spatial_reuse_time_start, m_renderer->get_main_stream()));

	for (int spatial_reuse_pass = 0; spatial_reuse_pass < render_data->render_settings.restir_di_settings.spatial_pass.number_of_passes; spatial_reuse_pass++)
	{
		configure_spatial_pass(spatial_reuse_pass);
		m_kernels[ReSTIRDIRenderPass::RESTIR_DI_SPATIAL_REUSE_KERNEL_ID].launch_asynchronous(KernelBlockWidthHeight, KernelBlockWidthHeight, render_resolution.x, render_resolution.y, launch_args, m_renderer->get_main_stream());
	}

	// Emitting the stop event
	OROCHI_CHECK_ERROR(oroEventRecord(spatial_reuse_time_stop, m_renderer->get_main_stream()));
}

void ReSTIRDIRenderPass::configure_spatiotemporal_pass()
{
	// The buffers of the temporal pass are going to be configured in the same way
	configure_temporal_pass_for_fused_spatiotemporal();

	// But the spatial pass is going to read from the input of the temporal pass i.e. the temporal buffer of the last frame, it's not going to read from the output of the temporal pass
	configure_spatial_pass_for_fused_spatiotemporal(0);
}

void ReSTIRDIRenderPass::launch_spatiotemporal_pass()
{
	int2 render_resolution = m_renderer->m_render_resolution;
	void* launch_args[] = { &m_renderer->get_render_data(), &m_renderer->m_render_resolution };

	configure_spatiotemporal_pass();
	m_kernels[ReSTIRDIRenderPass::RESTIR_DI_SPATIOTEMPORAL_REUSE_KERNEL_ID].launch_asynchronous(KernelBlockWidthHeight, KernelBlockWidthHeight, render_resolution.x, render_resolution.y, launch_args, m_renderer->get_main_stream());

	if (render_data->render_settings.restir_di_settings.spatial_pass.number_of_passes > 1)
	{
		// We have some more spatial reuse passes to do

		OROCHI_CHECK_ERROR(oroEventRecord(spatial_reuse_time_start, m_renderer->get_main_stream()));
		for (int spatial_pass_index = 1; spatial_pass_index < render_data->render_settings.restir_di_settings.spatial_pass.number_of_passes; spatial_pass_index++)
		{
			configure_spatial_pass_for_fused_spatiotemporal(spatial_pass_index);
			m_kernels[ReSTIRDIRenderPass::RESTIR_DI_SPATIAL_REUSE_KERNEL_ID].launch_asynchronous(KernelBlockWidthHeight, KernelBlockWidthHeight, render_resolution.x, render_resolution.y, launch_args, m_renderer->get_main_stream());
		}

		// Emitting the stop event
		OROCHI_CHECK_ERROR(oroEventRecord(spatial_reuse_time_stop, m_renderer->get_main_stream()));
	}
}

void ReSTIRDIRenderPass::compute_render_times(std::unordered_map<std::string, float>& times)
{
	if (m_renderer->get_global_compiler_options()->get_macro_value(GPUKernelCompilerOptions::DIRECT_LIGHT_SAMPLING_STRATEGY) != LSS_RESTIR_DI)
		return;

	std::unordered_map<std::string, float>& ms_time_per_pass = m_renderer->get_render_pass_times();
	ReSTIRDISettings& restir_di_settings = render_data->render_settings.restir_di_settings;

	if (m_renderer->get_global_compiler_options()->get_macro_value(GPUKernelCompilerOptions::RESTIR_DI_DO_LIGHTS_PRESAMPLING) == KERNEL_OPTION_TRUE)
		ms_time_per_pass[ReSTIRDIRenderPass::RESTIR_DI_LIGHTS_PRESAMPLING_KERNEL_ID] = m_kernels[ReSTIRDIRenderPass::RESTIR_DI_LIGHTS_PRESAMPLING_KERNEL_ID].get_last_execution_time();

	ms_time_per_pass[ReSTIRDIRenderPass::RESTIR_DI_INITIAL_CANDIDATES_KERNEL_ID] = m_kernels[ReSTIRDIRenderPass::RESTIR_DI_INITIAL_CANDIDATES_KERNEL_ID].get_last_execution_time();
	if (restir_di_settings.do_fused_spatiotemporal)
	{
		ms_time_per_pass[ReSTIRDIRenderPass::RESTIR_DI_SPATIOTEMPORAL_REUSE_KERNEL_ID] = m_kernels[ReSTIRDIRenderPass::RESTIR_DI_SPATIOTEMPORAL_REUSE_KERNEL_ID].get_last_execution_time();
		if (restir_di_settings.spatial_pass.number_of_passes > 1)
			oroEventElapsedTime(&ms_time_per_pass[ReSTIRDIRenderPass::RESTIR_DI_SPATIAL_REUSE_KERNEL_ID], spatial_reuse_time_start, spatial_reuse_time_stop);
	}
	else
	{
		ms_time_per_pass[ReSTIRDIRenderPass::RESTIR_DI_TEMPORAL_REUSE_KERNEL_ID] = m_kernels[ReSTIRDIRenderPass::RESTIR_DI_TEMPORAL_REUSE_KERNEL_ID].get_last_execution_time();
		oroEventElapsedTime(&ms_time_per_pass[ReSTIRDIRenderPass::RESTIR_DI_SPATIAL_REUSE_KERNEL_ID], spatial_reuse_time_start, spatial_reuse_time_stop);
	}
}

void ReSTIRDIRenderPass::update_perf_metrics(std::shared_ptr<PerformanceMetricsComputer> perf_metrics)
{
	std::shared_ptr<GPUKernelCompilerOptions> compiler_options = m_renderer->get_global_compiler_options();
	std::unordered_map<std::string, float> render_pass_times = m_renderer->get_render_pass_times();

	if (compiler_options->get_macro_value(GPUKernelCompilerOptions::DIRECT_LIGHT_SAMPLING_STRATEGY) == LSS_RESTIR_DI)
	{
		if (compiler_options->get_macro_value(GPUKernelCompilerOptions::RESTIR_DI_DO_LIGHTS_PRESAMPLING) == KERNEL_OPTION_TRUE)
			perf_metrics->add_value(ReSTIRDIRenderPass::RESTIR_DI_LIGHTS_PRESAMPLING_KERNEL_ID, render_pass_times[ReSTIRDIRenderPass::RESTIR_DI_LIGHTS_PRESAMPLING_KERNEL_ID]);
		perf_metrics->add_value(ReSTIRDIRenderPass::RESTIR_DI_INITIAL_CANDIDATES_KERNEL_ID, render_pass_times[ReSTIRDIRenderPass::RESTIR_DI_INITIAL_CANDIDATES_KERNEL_ID]);

		ReSTIRDISettings& restir_di_settings = m_renderer->get_render_settings().restir_di_settings;
		if (restir_di_settings.do_fused_spatiotemporal)
		{
			perf_metrics->add_value(ReSTIRDIRenderPass::RESTIR_DI_SPATIOTEMPORAL_REUSE_KERNEL_ID, render_pass_times[ReSTIRDIRenderPass::RESTIR_DI_SPATIOTEMPORAL_REUSE_KERNEL_ID]);
			if (restir_di_settings.spatial_pass.number_of_passes > 1)
				perf_metrics->add_value(ReSTIRDIRenderPass::RESTIR_DI_SPATIAL_REUSE_KERNEL_ID, render_pass_times[ReSTIRDIRenderPass::RESTIR_DI_SPATIAL_REUSE_KERNEL_ID]);
		}
		else
		{
			perf_metrics->add_value(ReSTIRDIRenderPass::RESTIR_DI_TEMPORAL_REUSE_KERNEL_ID, render_pass_times[ReSTIRDIRenderPass::RESTIR_DI_TEMPORAL_REUSE_KERNEL_ID]);
			perf_metrics->add_value(ReSTIRDIRenderPass::RESTIR_DI_SPATIAL_REUSE_KERNEL_ID, render_pass_times[ReSTIRDIRenderPass::RESTIR_DI_SPATIAL_REUSE_KERNEL_ID]);
		}
	}
}

std::map<std::string, GPUKernel>& ReSTIRDIRenderPass::get_kernels()
{
	return m_kernels;
}

void ReSTIRDIRenderPass::configure_output_buffer()
{
	ReSTIRDISettings& restir_di_settings = render_data->render_settings.restir_di_settings;

	// Keeping in mind which was the buffer used last for the output of the spatial reuse pass as this is the buffer that
	// we're going to use as the input to the temporal reuse pass of the next frame
	if (restir_di_settings.spatial_pass.do_spatial_reuse_pass || restir_di_settings.do_fused_spatiotemporal)
		// If there was spatial reuse, using the output of the spatial reuse pass as the input of the temporal
		// pass of next frame
		restir_di_settings.restir_output_reservoirs = restir_di_settings.spatial_pass.output_reservoirs;
	else if (restir_di_settings.temporal_pass.do_temporal_reuse_pass)
		// If there was a temporal reuse pass, using that output as the input of the next temporal reuse pass
		restir_di_settings.restir_output_reservoirs = restir_di_settings.temporal_pass.output_reservoirs;
	else
		// No spatial or temporal, the output of ReSTIR is just the output of the initial candidates pass
		restir_di_settings.restir_output_reservoirs = restir_di_settings.initial_candidates.output_reservoirs;
}
