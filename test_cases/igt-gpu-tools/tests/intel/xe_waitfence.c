// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "igt.h"
#include "lib/igt_syncobj.h"
#include "lib/intel_reg.h"
#include "xe_drm.h"

#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"
#include "xe/xe_spin.h"
#include <string.h>

/**
 * TEST: Check if waitfences work
 * Category: Software building block
 * Sub-category: waitfence
 * Functionality: waitfence
 * Test category: functionality test
 * Description: Test waitfences functionality
 */

uint64_t wait_fence = 0;

static void do_bind(int fd, uint32_t vm, uint32_t bo, uint64_t offset,
		    uint64_t addr, uint64_t size, uint64_t val)
{
	struct drm_xe_sync sync[1] = {};
	sync[0].type = DRM_XE_SYNC_TYPE_USER_FENCE;
	sync[0].flags = DRM_XE_SYNC_FLAG_SIGNAL;

	sync[0].addr = to_user_pointer(&wait_fence);
	sync[0].timeline_value = val;
	xe_vm_bind_async(fd, vm, 0, bo, offset, addr, size, sync, 1);
}

static int64_t wait_with_eci_abstime(int fd, uint64_t *addr, uint64_t value,
				     uint32_t exec_queue, int64_t timeout,
				     uint16_t flag)
{
	struct drm_xe_wait_user_fence wait = {
		.addr = to_user_pointer(addr),
		.op = DRM_XE_UFENCE_WAIT_OP_EQ,
		.flags = flag,
		.value = value,
		.mask = DRM_XE_UFENCE_WAIT_MASK_U64,
		.timeout = timeout,
		.exec_queue_id = exec_queue,
	};
	struct timespec ts;

	igt_assert_eq(igt_ioctl(fd, DRM_IOCTL_XE_WAIT_USER_FENCE, &wait), 0);
	igt_assert_eq(clock_gettime(CLOCK_MONOTONIC, &ts), 0);

	return ts.tv_sec * 1e9 + ts.tv_nsec;
}

enum waittype {
	RELTIME,
	ABSTIME,
	ENGINE,
};

/**
 * SUBTEST: reltime
 * Description: Check basic waitfences functionality with timeout
 *              as relative timeout in nanoseconds
 *
 * SUBTEST: abstime
 * Description: Check basic waitfences functionality with timeout
 *              passed as absolute time in nanoseconds
 *
 * SUBTEST: engine
 * Description: Check basic waitfences functionality with timeout
 *              passed as absolute time in nanoseconds and provide engine class
 *              instance
 */
static void
waitfence(int fd, enum waittype wt)
{
	uint32_t exec_queue;
	struct timespec ts;
	int64_t current, signalled;
	uint32_t bo_1;
	uint32_t bo_2;
	uint32_t bo_3;
	uint32_t bo_4;
	uint32_t bo_5;
	uint32_t bo_6;
	uint32_t bo_7;
	int64_t timeout;

	uint32_t vm = xe_vm_create(fd, 0, 0);
	bo_1 = xe_bo_create(fd, vm, 0x40000, vram_if_possible(fd, 0), 0);
	do_bind(fd, vm, bo_1, 0, 0x200000, 0x40000, 1);
	bo_2 = xe_bo_create(fd, vm, 0x40000, vram_if_possible(fd, 0), 0);
	do_bind(fd, vm, bo_2, 0, 0xc0000000, 0x40000, 2);
	bo_3 = xe_bo_create(fd, vm, 0x40000, vram_if_possible(fd, 0), 0);
	do_bind(fd, vm, bo_3, 0, 0x180000000, 0x40000, 3);
	bo_4 = xe_bo_create(fd, vm, 0x10000, vram_if_possible(fd, 0), 0);
	do_bind(fd, vm, bo_4, 0, 0x140000000, 0x10000, 4);
	bo_5 = xe_bo_create(fd, vm, 0x100000, vram_if_possible(fd, 0), 0);
	do_bind(fd, vm, bo_5, 0, 0x100000000, 0x100000, 5);
	bo_6 = xe_bo_create(fd, vm, 0x1c0000, vram_if_possible(fd, 0), 0);
	do_bind(fd, vm, bo_6, 0, 0xc0040000, 0x1c0000, 6);
	bo_7 = xe_bo_create(fd, vm, 0x10000, vram_if_possible(fd, 0), 0);
	do_bind(fd, vm, bo_7, 0, 0xeffff0000, 0x10000, 7);

	if (wt == RELTIME) {
		timeout = xe_wait_ufence(fd, &wait_fence, 7, 0, MS_TO_NS(10));
		igt_debug("wait type: RELTIME - timeout: %ld, timeout left: %ld\n",
			  MS_TO_NS(10), timeout);
	} else if (wt == ENGINE) {
		exec_queue = xe_exec_queue_create_class(fd, vm, DRM_XE_ENGINE_CLASS_COPY);
		clock_gettime(CLOCK_MONOTONIC, &ts);
		current = ts.tv_sec * 1e9 + ts.tv_nsec;
		timeout = current + MS_TO_NS(10);
		signalled = wait_with_eci_abstime(fd, &wait_fence, 7,
						  exec_queue, timeout,
						  DRM_XE_UFENCE_WAIT_FLAG_ABSTIME);
		igt_debug("wait type: ENGINE ABSTIME - timeout: %" PRId64
			  ", signalled: %" PRId64
			  ", elapsed: %" PRId64 "\n",
			  timeout, signalled, signalled - current);
	} else {
		clock_gettime(CLOCK_MONOTONIC, &ts);
		current = ts.tv_sec * 1e9 + ts.tv_nsec;
		timeout = current + MS_TO_NS(10);
		signalled = xe_wait_ufence_abstime(fd, &wait_fence, 7, 0,
						   timeout, 0);
		igt_debug("wait type: ABSTIME - timeout: %" PRId64
			  ", signalled: %" PRId64
			  ", elapsed: %" PRId64 "\n",
			  timeout, signalled, signalled - current);
	}
}

/**
 * TEST: Negative test for wait ufence ioctl
 * Category: Software building block
 * Sub-category: waitfence
 * Functionality: waitfence
 * Run type: FULL
 * Test category: negative test
 *
 * SUBTEST: invalid-flag
 * Description: Check query with invalid flag returns expected error code
 *
 * SUBTEST: invalid-ops
 * Description: Check query with invalid ops returns expected error code
 *
 * SUBTEST: invalid-engine
 * Description: Check query with invalid engine info returns expected error code
 *
 * SUBTEST: exec_queue-reset-wait
 * Description: Don’t wait till timeout on user fence when exec_queue reset is detected and return return proper error
 */

static void
invalid_flag(int fd)
{
	uint32_t bo;

	struct drm_xe_wait_user_fence wait = {
		.addr = to_user_pointer(&wait_fence),
		.op = DRM_XE_UFENCE_WAIT_OP_EQ,
		.flags = -1,
		.value = 1,
		.mask = DRM_XE_UFENCE_WAIT_MASK_U64,
		.timeout = -1,
		.exec_queue_id = 0,
	};

	uint32_t vm = xe_vm_create(fd, 0, 0);

	bo = xe_bo_create(fd, vm, 0x40000, vram_if_possible(fd, 0), 0);

	do_bind(fd, vm, bo, 0, 0x200000, 0x40000, 1);

	do_ioctl_err(fd, DRM_IOCTL_XE_WAIT_USER_FENCE, &wait, EINVAL);
}

static void
invalid_ops(int fd)
{
	uint32_t bo;

	struct drm_xe_wait_user_fence wait = {
		.addr = to_user_pointer(&wait_fence),
		.op = -1,
		.flags = 0,
		.value = 1,
		.mask = DRM_XE_UFENCE_WAIT_MASK_U64,
		.timeout = 1,
		.exec_queue_id = 0,
	};

	uint32_t vm = xe_vm_create(fd, 0, 0);

	bo = xe_bo_create(fd, vm, 0x40000, vram_if_possible(fd, 0), 0);

	do_bind(fd, vm, bo, 0, 0x200000, 0x40000, 1);

	do_ioctl_err(fd, DRM_IOCTL_XE_WAIT_USER_FENCE, &wait, EINVAL);
}

static void
invalid_engine(int fd)
{
	uint32_t bo;

	struct drm_xe_wait_user_fence wait = {
		.addr = to_user_pointer(&wait_fence),
		.op = DRM_XE_UFENCE_WAIT_OP_EQ,
		.flags = 0,
		.value = 1,
		.mask = DRM_XE_UFENCE_WAIT_MASK_U64,
		.timeout = -1,
		.exec_queue_id = 0,
	};

	uint32_t vm = xe_vm_create(fd, 0, 0);

	bo = xe_bo_create(fd, vm, 0x40000, vram_if_possible(fd, 0), 0);

	do_bind(fd, vm, bo, 0, 0x200000, 0x40000, 1);

	do_ioctl_err(fd, DRM_IOCTL_XE_WAIT_USER_FENCE, &wait, EFAULT);
}

static void
exec_queue_reset_wait(int fd)
{
	uint32_t bo, b;
	uint64_t batch_offset;
	uint64_t batch_addr;
	uint64_t sdi_offset;
	uint64_t sdi_addr;
	uint64_t addr = 0x1a0000;

	struct {
		uint32_t batch[16];
		uint64_t pad;
		uint64_t vm_sync;
		uint64_t exec_sync;
		uint32_t data;
	} *data;

#define USER_FENCE_VALUE        0xdeadbeefdeadbeefull
	struct drm_xe_sync sync[1] = {
		{ .flags = DRM_XE_SYNC_TYPE_USER_FENCE | DRM_XE_SYNC_FLAG_SIGNAL,
			.timeline_value = USER_FENCE_VALUE },
	};

	struct drm_xe_exec exec = {
		.num_batch_buffer = 1,
		.num_syncs = 1,
		.syncs = to_user_pointer(sync),
	};

	uint32_t vm = xe_vm_create(fd, 0, 0);
	uint32_t exec_queue = xe_exec_queue_create_class(fd, vm, DRM_XE_ENGINE_CLASS_COPY);
	struct drm_xe_wait_user_fence wait = {
		.op = DRM_XE_UFENCE_WAIT_OP_EQ,
		.flags = 0,
		.value = 0xc0ffee,
		.mask = DRM_XE_UFENCE_WAIT_MASK_U64,
		.timeout = -1,
		.exec_queue_id = exec_queue,
	};

	bo = xe_bo_create(fd, vm, 0x40000, vram_if_possible(fd, 0), 0);
	data = xe_bo_map(fd, bo, 0x40000);

	batch_offset = (char *)&data[0].batch - (char *)data;
	batch_addr = addr + batch_offset;
	sdi_offset = (char *)&data[0].data - (char *)data;
	sdi_addr = addr + sdi_offset;

	b = 0;
	data[0].batch[b++] = MI_STORE_DWORD_IMM_GEN4;
	data[0].batch[b++] = sdi_addr;
	data[0].batch[b++] = sdi_addr >> 32;
	data[0].batch[b++] = 0xc0ffee;
	data[0].batch[b++] = MI_BATCH_BUFFER_END;
	igt_assert(b <= ARRAY_SIZE(data[0].batch));

	wait.addr = to_user_pointer(&data[0].exec_sync);
	exec.exec_queue_id = exec_queue;
	exec.address = batch_addr;

	xe_exec(fd, &exec);

	/**
	  * Don't do the GPU mapping(vm_bind) for object, so that exec_queue
	  * reset will happen and xe_wait_ufence will return EIO not ETIME
	  */
	do_ioctl_err(fd, DRM_IOCTL_XE_WAIT_USER_FENCE, &wait, EIO);

	xe_exec_queue_destroy(fd, exec_queue);

	if (bo) {
		munmap(data, 0x40000);
		gem_close(fd, bo);
	}
}

igt_main
{
	int fd;

	igt_fixture
		fd = drm_open_driver(DRIVER_XE);

	igt_subtest("reltime")
		waitfence(fd, RELTIME);

	igt_subtest("abstime")
		waitfence(fd, ABSTIME);

	igt_subtest("engine")
		waitfence(fd, ENGINE);

	igt_subtest("invalid-flag")
		invalid_flag(fd);

	igt_subtest("invalid-ops")
		invalid_ops(fd);

	igt_subtest("invalid-engine")
		invalid_engine(fd);

	igt_subtest("exec_queue-reset-wait")
		exec_queue_reset_wait(fd);

	igt_fixture
		drm_close_driver(fd);
}
