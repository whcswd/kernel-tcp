// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

/**
 * TEST: Check bo create ioctl
 * Category: Software building block
 * Sub-category: uapi
 */

#include <string.h>

#include "igt.h"
#include "igt_syncobj.h"
#include "xe_drm.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"

#define PAGE_SIZE 0x1000

static struct param {
	unsigned int quantity;
	unsigned int percent;
} params = {
	.quantity = 0,
	.percent = 100,
};

static int __ioctl_create(int fd, struct drm_xe_gem_create *create)
{
	int ret = 0;

	if (igt_ioctl(fd, DRM_IOCTL_XE_GEM_CREATE, create)) {
		ret = -errno;
		errno = 0;
	}

	return ret;
}

static int __create_bo(int fd, uint32_t vm, uint64_t size, uint32_t placement,
		       uint32_t *handlep)
{
	struct drm_xe_gem_create create = {
		.vm_id = vm,
		.size = size,
		.cpu_caching = __xe_default_cpu_caching(fd, placement, 0),
		.placement = placement,
	};
	int ret;

	igt_assert(handlep);

	ret = __ioctl_create(fd, &create);
	*handlep = create.handle;

	return ret;
}

/**
 * SUBTEST: create-invalid-size
 * Functionality: ioctl
 * Test category: negative test
 * Description: Verifies xe bo create returns expected error code on invalid
 *              buffer sizes.
 */
static void create_invalid_size(int fd)
{
	struct drm_xe_mem_region *memregion;
	uint64_t memreg = all_memory_regions(fd), region;
	uint32_t vm;
	uint32_t handle;
	int ret;

	vm = xe_vm_create(fd, 0, 0);

	xe_for_each_mem_region(fd, memreg, region) {
		memregion = xe_mem_region(fd, region);

		/* first try, use half of possible min page size */
		ret = __create_bo(fd, vm, memregion->min_page_size >> 1,
				  region, &handle);
		if (!ret) {
			gem_close(fd, handle);
			xe_vm_destroy(fd, vm);
		}
		igt_assert_eq(ret, -EINVAL);

		/*
		 * second try, add page size to min page size if it is
		 * bigger than page size.
		 */
		if (memregion->min_page_size > PAGE_SIZE) {
			ret = __create_bo(fd, vm,
					  memregion->min_page_size + PAGE_SIZE,
					  region, &handle);
			if (!ret) {
				gem_close(fd, handle);
				xe_vm_destroy(fd, vm);
			}
			igt_assert_eq(ret, -EINVAL);
		}
	}

	xe_vm_destroy(fd, vm);
}

/**
 * SUBTEST: create-invalid-mbz
 * Functionality: ioctl
 * Test category: negative test
 * Description: Verifies xe bo create returns expected error code on all MBZ fields.
 */
static void create_invalid_mbz(int fd)
{
	struct drm_xe_gem_create create = {
		.size = PAGE_SIZE,
		.placement = system_memory(fd),
		.cpu_caching = DRM_XE_GEM_CPU_CACHING_WB,
	};
	int i;

	/* Make sure the baseline passes */
	igt_assert_eq(__ioctl_create(fd, &create), 0);
	gem_close(fd, create.handle);
	create.handle = 0;

	/* No supported extensions yet */
	create.extensions = -1;
	igt_assert_eq(__ioctl_create(fd, &create), -EINVAL);
	create.extensions = 0;

	/* Make sure KMD rejects non-zero padding/reserved fields */
	for (i = 0; i < ARRAY_SIZE(create.pad); i++) {
		create.pad[i] = -1;
		igt_assert_eq(__ioctl_create(fd, &create), -EINVAL);
		create.pad[i] = 0;
	}

	for (i = 0; i < ARRAY_SIZE(create.reserved); i++) {
		create.reserved[i] = -1;
		igt_assert_eq(__ioctl_create(fd, &create), -EINVAL);
		create.reserved[i] = 0;
	}
}

enum exec_queue_destroy {
	NOLEAK,
	LEAK
};

enum vm_count {
	MULTI,
	SHARED
};

#define MAXEXECQUEUES 2048
#define MAXTIME 5

/**
 * SUBTEST: create-execqueues-%s
 * Functionality: exequeues creation time
 * Description: Check process ability of multiple exec_queues creation
 * Test category: functionality test
 *
 * arg[1]:
 *
 * @noleak:				destroy exec_queues in the code
 * @leak:				destroy exec_queues in close() path
 * @noleak-shared:			same as noleak, but with a shared vm
 * @leak-shared:			same as leak, but with a shared vm
 */
static void create_execqueues(int fd, enum exec_queue_destroy ed,
			      enum vm_count vc)
{
	struct timespec tv = { };
	uint32_t num_engines, exec_queues_per_process, vm;
	int nproc = sysconf(_SC_NPROCESSORS_ONLN), seconds;
	int real_timeout = MAXTIME * (vc == SHARED ? 4 : 1);

	if (vc == SHARED) {
		fd = drm_reopen_driver(fd);
		num_engines = xe_number_engines(fd);
		vm = xe_vm_create(fd, 0, 0);
	}

	exec_queues_per_process = max_t(uint32_t, 1, MAXEXECQUEUES / nproc);
	igt_debug("nproc: %u, exec_queues per process: %u\n", nproc, exec_queues_per_process);

	igt_nsec_elapsed(&tv);

	igt_fork(n, nproc) {
		struct drm_xe_engine *engine;
		uint32_t exec_queue, exec_queues[exec_queues_per_process];
		int idx, err, i;

		if (vc == MULTI) {
			fd = drm_reopen_driver(fd);
			num_engines = xe_number_engines(fd);
			vm = xe_vm_create(fd, 0, 0);
		}

		srandom(n);

		for (i = 0; i < exec_queues_per_process; i++) {
			idx = rand() % num_engines;
			engine = xe_engine(fd, idx);
			err = __xe_exec_queue_create(fd, vm, &engine->instance,
						     0, &exec_queue);
			igt_debug("[%2d] Create exec_queue: err=%d, exec_queue=%u [idx = %d]\n",
				  n, err, exec_queue, i);
			if (err)
				break;

			if (ed == NOLEAK)
				exec_queues[i] = exec_queue;
		}

		if (ed == NOLEAK) {
			while (--i >= 0) {
				igt_debug("[%2d] Destroy exec_queue: %u\n", n, exec_queues[i]);
				xe_exec_queue_destroy(fd, exec_queues[i]);
			}
		}

		if (vc == MULTI) {
			xe_vm_destroy(fd, vm);
			drm_close_driver(fd);
		}
	}
	igt_waitchildren();

	if (vc == SHARED) {
		xe_vm_destroy(fd, vm);
		drm_close_driver(fd);
	}

	seconds = igt_seconds_elapsed(&tv);
	igt_assert_f(seconds < real_timeout,
		     "Creating %d exec_queues tooks too long: %d [limit: %d]\n",
		     MAXEXECQUEUES, seconds, real_timeout);
}

/**
 * SUBTEST: create-massive-size
 * Functionality: ioctl
 * Test category: functionality test
 * Description: Verifies xe bo create returns expected error code on massive
 *              buffer sizes.
 *
 * SUBTEST: multigpu-create-massive-size
 * Functionality: ioctl
 * Test category: functionality test
 * Feature: multigpu
 * Description: Verifies xe bo create with massive buffer sizes runs correctly
 *		on two or more GPUs.
 */
static void create_massive_size(int fd)
{
	uint64_t memreg = all_memory_regions(fd), region;
	uint32_t vm;
	uint32_t handle;
	int ret;

	vm = xe_vm_create(fd, 0, 0);

	xe_for_each_mem_region(fd, memreg, region) {
		ret = __create_bo(fd, vm, -1ULL << 32, region, &handle);
		igt_assert_eq(ret, -ENOSPC);
	}
}

/**
 * SUBTEST: create-big-vram
 * Functionality: BO creation
 * Test category: functionality test
 * Description: Verifies the creation of substantial BO within VRAM,
 *		constituting all available CPU-visible VRAM.
 */
static void create_big_vram(int fd, int gt)
{
	uint64_t bo_size, size, visible_avail_size, alignment;
	uint32_t bo_handle;
	char *bo_ptr = NULL;
	uint64_t vm = 0;

	alignment = xe_get_default_alignment(fd);
	vm = xe_vm_create(fd, 0, 0);

	visible_avail_size = xe_visible_available_vram_size(fd, gt);
	igt_require(visible_avail_size);

	bo_size = params.quantity ? params.quantity * 1024ULL * 1024ULL
		  : ALIGN_DOWN(visible_avail_size * params.percent / 100, alignment);
	igt_require(bo_size);
	igt_info("gt%u bo_size=%lu visible_available_vram_size=%lu\n",
		gt, bo_size, visible_avail_size);

	bo_handle = xe_bo_create(fd, vm, bo_size, vram_if_possible(fd, gt),
				 DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM);
	bo_ptr = xe_bo_map(fd, bo_handle, bo_size);

	size = bo_size - 1;
	while (size > SZ_64K) {
		igt_assert_eq(0, READ_ONCE(bo_ptr[size]));
		WRITE_ONCE(bo_ptr[size], 'A');
		igt_assert_eq('A', READ_ONCE(bo_ptr[size]));
		size >>= 1;
	}
	igt_assert_eq(0, bo_ptr[0]);

	munmap(bo_ptr, bo_size);
	gem_close(fd, bo_handle);
	xe_vm_destroy(fd, vm);
}

/**
 * SUBTEST: create-contexts
 * Functionality: contexts creation
 * Test category: functionality test
 * Description: Verifies the creation of substantial number of HW contexts
 *		(4096 as default).
 */
static void create_contexts(int fd)
{
	unsigned int i, n = params.quantity ? params.quantity : 4096;
	uint64_t bo_size = xe_get_default_alignment(fd), bo_addr = 0x1a0000;
	uint32_t vm, bo, *batch, exec_queues[n];
	struct drm_xe_sync sync = {
		.type = DRM_XE_SYNC_TYPE_SYNCOBJ,
		.flags = DRM_XE_SYNC_FLAG_SIGNAL,
		.handle = syncobj_create(fd, 0),
	};
	struct drm_xe_exec exec = {
		.num_syncs = 1,
		.syncs = to_user_pointer(&sync),
		.address = bo_addr,
		.num_batch_buffer = 1,
	};

	vm = xe_vm_create(fd, DRM_XE_VM_CREATE_FLAG_SCRATCH_PAGE, 0);
	bo = xe_bo_create(fd, vm, bo_size, system_memory(fd), 0);

	batch = xe_bo_map(fd, bo, bo_size);
	*batch = MI_BATCH_BUFFER_END;
	munmap(batch, bo_size);

	xe_vm_bind_sync(fd, vm, bo, 0, bo_addr, bo_size);

	for (i = 0; i < n; i++) {
		int err = __xe_exec_queue_create(fd, vm, &xe_engine(fd, 0)->instance, 0,
						 &exec_queues[i]);
		igt_assert_f(!err, "Failed to create exec queue (%d), iteration: %d\n", err,
			     i + 1);

		exec.exec_queue_id = exec_queues[i];
		err = __xe_exec(fd, &exec);
		igt_assert_f(!err, "Failed to execute batch (%d), iteration: %d\n", err, i + 1);

		err = syncobj_wait(fd, &sync.handle, 1, INT64_MAX, 0, NULL);
		igt_assert_f(err, "Timeout while waiting for syncobj signal, iteration: %d\n",
			     i + 1);
	}

	for (i = 0; i < n; i++)
		xe_exec_queue_destroy(fd, exec_queues[i]);
	gem_close(fd, bo);
	xe_vm_destroy(fd, vm);
	syncobj_destroy(fd, sync.handle);
}

static int opt_handler(int opt, int opt_index, void *data)
{
	switch (opt) {
	case 'Q':
		params.quantity = atoi(optarg);
		igt_debug("Resource quantity (memory in MB): %d\n", params.quantity);
		break;
	case 'p':
		params.percent = atoi(optarg);
		igt_debug("Percent of available resource: %d\n", params.percent);
		break;
	default:
		return IGT_OPT_HANDLER_ERROR;
	}

	return IGT_OPT_HANDLER_SUCCESS;
}

const char *help_str =
	"  -Q\tresource quantity (memory in MB)\n"
	"  -p\tpercent of available resource\n"
	;

igt_main_args("Q:p:", NULL, help_str, opt_handler, NULL)
{
	int xe;

	igt_fixture
		xe = drm_open_driver(DRIVER_XE);

	igt_subtest("create-invalid-mbz")
		create_invalid_mbz(xe);

	igt_subtest("create-invalid-size") {
		create_invalid_size(xe);
	}

	igt_subtest("create-execqueues-noleak")
		create_execqueues(xe, NOLEAK, MULTI);

	igt_subtest("create-execqueues-leak")
		create_execqueues(xe, LEAK, MULTI);

	igt_subtest("create-execqueues-noleak-shared")
		create_execqueues(xe, NOLEAK, SHARED);

	igt_subtest("create-execqueues-leak-shared")
		create_execqueues(xe, LEAK, SHARED);

	igt_subtest("create-massive-size") {
		create_massive_size(xe);
	}

	igt_subtest_with_dynamic("create-big-vram") {
		int gt;

		igt_require(xe_has_vram(xe));

		xe_for_each_gt(xe, gt)
			igt_dynamic_f("gt%u", gt)
				create_big_vram(xe, gt);
	}

	igt_subtest("create-contexts") {
		create_contexts(xe);
	}

	igt_subtest("multigpu-create-massive-size") {
		int gpu_count = drm_prepare_filtered_multigpu(DRIVER_XE);

		igt_require(xe > 0);
		igt_require(gpu_count >= 2);
		igt_multi_fork(child, gpu_count) {
			int gpu_fd;

			gpu_fd = drm_open_filtered_card(child);
			igt_assert_f(gpu_fd > 0, "cannot open gpu-%d, errno=%d\n", child, errno);
			igt_assert(is_xe_device(gpu_fd));

			create_massive_size(gpu_fd);
			drm_close_driver(gpu_fd);
		}
		igt_waitchildren();
	}

	igt_fixture
		drm_close_driver(xe);
}
