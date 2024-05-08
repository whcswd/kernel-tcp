// SPDX-License-Identifier: MIT
/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 * Copyright 2022 Advanced Micro Devices, Inc.
 * Copyright 2023 Advanced Micro Devices, Inc.
 */

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include "lib/amdgpu/amd_memory.h"
#include "lib/amdgpu/amd_command_submission.h"
#include "lib/amdgpu/amd_dispatch.h"

static void
amdgpu_dispatch_hang_slow_gfx(amdgpu_device_handle device_handle)
{
	amdgpu_dispatch_hang_slow_helper(device_handle, AMDGPU_HW_IP_GFX);
}

static void
amdgpu_dispatch_hang_slow_compute(amdgpu_device_handle device_handle)
{
	amdgpu_dispatch_hang_slow_helper(device_handle, AMDGPU_HW_IP_COMPUTE);
}

static void
amdgpu_dispatch_hang_gfx(amdgpu_device_handle device_handle)
{
	amdgpu_gfx_dispatch_test(device_handle, AMDGPU_HW_IP_GFX, 1);
}

static void
amdgpu_dispatch_hang_compute(amdgpu_device_handle device_handle)
{
	amdgpu_gfx_dispatch_test(device_handle, AMDGPU_HW_IP_COMPUTE, 1);
}

static void
amdgpu_gpu_reset_test(amdgpu_device_handle device_handle, int drm_amdgpu)
{
	amdgpu_context_handle context_handle;
	char debugfs_path[256], tmp[10];
	uint32_t hang_state, hangs;
	struct stat sbuf;
	int r, fd;

	r = amdgpu_cs_ctx_create(device_handle, &context_handle);
	igt_assert_eq(r, 0);

	r = fstat(drm_amdgpu, &sbuf);
	igt_assert_eq(r, 0);

	sprintf(debugfs_path, "/sys/kernel/debug/dri/%d/amdgpu_gpu_recover", minor(sbuf.st_rdev));
	fd = open(debugfs_path, O_RDONLY);
	igt_assert_fd(fd);

	r = read(fd, tmp, ARRAY_SIZE(tmp));
	igt_assert_lt(0, r);

	r = amdgpu_cs_query_reset_state(context_handle, &hang_state, &hangs);
	igt_assert_eq(r, 0);
	igt_assert_eq(hang_state, AMDGPU_CTX_UNKNOWN_RESET);

	close(fd);
	r = amdgpu_cs_ctx_free(context_handle);
	igt_assert_eq(r, 0);

	amdgpu_gfx_dispatch_test(device_handle, AMDGPU_HW_IP_GFX, 0);
	amdgpu_gfx_dispatch_test(device_handle, AMDGPU_HW_IP_COMPUTE, 0);
}

igt_main
{
	amdgpu_device_handle device;
	struct amdgpu_gpu_info gpu_info = {0};
	int fd = -1;
	int r;
	bool arr_cap[AMD_IP_MAX] = {0};

	igt_fixture {
		uint32_t major, minor;
		int err;

		fd = drm_open_driver(DRIVER_AMDGPU);

		err = amdgpu_device_initialize(fd, &major, &minor, &device);
		igt_require(err == 0);

		igt_info("Initialized amdgpu, driver version %d.%d\n",
			 major, minor);

		r = amdgpu_query_gpu_info(device, &gpu_info);
		igt_assert_eq(r, 0);
		r = setup_amdgpu_ip_blocks(major, minor, &gpu_info, device);
		igt_assert_eq(r, 0);
		asic_rings_readness(device, 1, arr_cap);

	}
	igt_describe("Test GPU reset using a binary shader to slow hang the job on compute ring");
	igt_subtest_with_dynamic("amdgpu-dispatch-test-compute-with-IP-COMPUTE") {
		if (arr_cap[AMD_IP_COMPUTE]) {
			igt_dynamic_f("amdgpu-dispatch-test-compute")
			amdgpu_dispatch_hang_slow_compute(device);
		}
	}

	igt_describe("Test GPU reset using a binary shader to slow hang the job on gfx ring");
	igt_subtest_with_dynamic("amdgpu-dispatch-test-gfx-with-IP-GFX") {
		if (arr_cap[AMD_IP_GFX]) {
			igt_dynamic_f("amdgpu-dispatch-test-gfx")
			 amdgpu_dispatch_hang_slow_gfx(device);
		}
	}

	igt_describe("Test GPU reset using a binary shader to hang the job on gfx ring");
	igt_subtest_with_dynamic("amdgpu-dispatch-hang-test-gfx-with-IP-GFX") {
		if (arr_cap[AMD_IP_GFX]) {
			igt_dynamic_f("amdgpu-dispatch-hang-test-gfx")
			amdgpu_dispatch_hang_gfx(device);
		}
	}

	igt_describe("Test GPU reset using a binary shader to hang the job on compute ring");
	igt_subtest_with_dynamic("amdgpu-dispatch-hang-test-compute-with-IP-COMPUTE") {
		if (arr_cap[AMD_IP_COMPUTE]) {
			igt_dynamic_f("amdgpu-dispatch-hang-test-compute")
			amdgpu_dispatch_hang_compute(device);
		}
	}

	igt_describe("Test GPU reset using amdgpu debugfs to hang the job on gfx ring");
	igt_subtest_with_dynamic("amdgpu-reset-test-gfx-with-IP-GFX-and-COMPUTE") {
		if (arr_cap[AMD_IP_GFX] && arr_cap[AMD_IP_COMPUTE]) {
			igt_dynamic_f("amdgpu-reset-gfx-compute")
			amdgpu_gpu_reset_test(device, fd);
		}
	}

	igt_fixture {
		amdgpu_device_deinitialize(device);
		drm_close_driver(fd);
	}
}
