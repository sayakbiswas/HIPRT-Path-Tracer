/*
 * Copyright 2024 Tom Clabault. GNU GPL3 license.
 * GNU GPL3 license copy: https://www.gnu.org/licenses/gpl-3.0.txt
 */

#ifndef RENDERER_NEE_PLUS_PLUS_CPU_GPU_COMMON_DATA_H
#define RENDERER_NEE_PLUS_PLUS_CPU_GPU_COMMON_DATA_H

#include "Device/includes/NEE++/NEE++.h" // For NEEPlusPlusDevice::NEE_PLUS_PLUS_DEFAULT_GRID_SIZE
#include "Scene/BoundingBox.h"

struct NEEPlusPlusCPUGPUCommonData
{
	unsigned int get_vram_usage_bytes() const
	{
		// Number of elements per matrix * sizeof(unsigned int) bytes
		return get_visibility_matrix_element_count(grid_dimensions_no_envmap + make_int3(2, 2, 2)) * sizeof(unsigned int);
	}

	unsigned int get_visibility_matrix_element_count(int3 dimensions) const
	{
		unsigned int grid_elements_count = dimensions.x * dimensions.y * dimensions.z;

		// Dividing by 2 because the visibility map is symmetrical so we only need half of the matrix
		unsigned half_matrix_size = grid_elements_count * (grid_elements_count + 1) / 2;

		return half_matrix_size;
	}

	void get_grid_extents(int3 base_grid_dimensions, float3& out_min_grid_point, float3& out_max_grid_point)
	{
		out_min_grid_point = base_grid_min_point;
		out_max_grid_point = base_grid_max_point;

		// Adding the envmap layer
		float3 one_voxel_size = (base_grid_max_point - base_grid_min_point) / make_float3(base_grid_dimensions.x, base_grid_dimensions.y, base_grid_dimensions.z);
		out_min_grid_point -= one_voxel_size;
		out_max_grid_point += one_voxel_size;
	}

	int3 get_grid_dimensions_with_envmap() const
	{
		return grid_dimensions_no_envmap + make_int3(2, 2, 2);
	}

	// Dimensions of the visibility map **without the envmap layer**
	int3 grid_dimensions_no_envmap = make_int3(NEEPlusPlusDevice::NEE_PLUS_PLUS_DEFAULT_GRID_SIZE, NEEPlusPlusDevice::NEE_PLUS_PLUS_DEFAULT_GRID_SIZE, NEEPlusPlusDevice::NEE_PLUS_PLUS_DEFAULT_GRID_SIZE);

	float3 base_grid_min_point, base_grid_max_point;

	// After how many samples to stop updating the visibility map
	// (because it's probably converged enough)
	int stop_update_samples = 64;
};

#endif