// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

/**
 * TEST: Basic tests for execbuf functionality for virtual and parallel exec_queues
 * Category: Hardware building block
 * Sub-category: execbuf
 * Functionality: virtual and parallel exec_queues
 * Test category: functionality test
 */

#include <fcntl.h>

#include "igt.h"
#include "lib/igt_syncobj.h"
#include "lib/intel_reg.h"
#include "xe_drm.h"

#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"
#include "xe/xe_spin.h"
#include <string.h>

#define MAX_INSTANCE 9

/**
 * SUBTEST: virtual-all-active
 * Description:
 * 	Run a test to check if virtual exec_queues can be running on all instances
 *	of a class simultaneously
 * Test category: functionality test
 */
static void test_all_active(int fd, int gt, int class)
{
	uint32_t vm;
	uint64_t addr = 0x1a0000;
	struct drm_xe_sync sync[2] = {
		{ .type = DRM_XE_SYNC_TYPE_SYNCOBJ, .flags = DRM_XE_SYNC_FLAG_SIGNAL, },
		{ .type = DRM_XE_SYNC_TYPE_SYNCOBJ, .flags = DRM_XE_SYNC_FLAG_SIGNAL, },
	};
	struct drm_xe_exec exec = {
		.num_batch_buffer = 1,
		.num_syncs = 2,
		.syncs = to_user_pointer(sync),
	};
	uint32_t exec_queues[MAX_INSTANCE];
	uint32_t syncobjs[MAX_INSTANCE];
	size_t bo_size;
	uint32_t bo = 0;
	struct {
		struct xe_spin spin;
	} *data;
	struct xe_spin_opts spin_opts = { .preempt = false };
	struct drm_xe_engine_class_instance *hwe;
	struct drm_xe_engine_class_instance eci[MAX_INSTANCE];
	int i, num_placements = 0;

	xe_for_each_engine(fd, hwe) {
		if (hwe->engine_class != class || hwe->gt_id != gt)
			continue;

		eci[num_placements++] = *hwe;
	}
	if (num_placements < 2)
		return;

	vm = xe_vm_create(fd, 0, 0);
	bo_size = sizeof(*data) * num_placements;
	bo_size = ALIGN(bo_size + xe_cs_prefetch_size(fd), xe_get_default_alignment(fd));

	bo = xe_bo_create(fd, vm, bo_size, vram_if_possible(fd, gt),
			  DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM);
	data = xe_bo_map(fd, bo, bo_size);

	for (i = 0; i < num_placements; i++) {
		struct drm_xe_exec_queue_create create = {
			.vm_id = vm,
			.width = 1,
			.num_placements = num_placements,
			.instances = to_user_pointer(eci),
		};

		igt_assert_eq(igt_ioctl(fd, DRM_IOCTL_XE_EXEC_QUEUE_CREATE,
					&create), 0);
		exec_queues[i] = create.exec_queue_id;
		syncobjs[i] = syncobj_create(fd, 0);
	};

	sync[0].handle = syncobj_create(fd, 0);
	xe_vm_bind_async(fd, vm, 0, bo, 0, addr, bo_size, sync, 1);

	for (i = 0; i < num_placements; i++) {
		spin_opts.addr = addr + (char *)&data[i].spin - (char *)data;
		xe_spin_init(&data[i].spin, &spin_opts);
		sync[0].flags &= ~DRM_XE_SYNC_FLAG_SIGNAL;
		sync[1].flags |= DRM_XE_SYNC_FLAG_SIGNAL;
		sync[1].handle = syncobjs[i];

		exec.exec_queue_id = exec_queues[i];
		exec.address = spin_opts.addr;
		xe_exec(fd, &exec);
		xe_spin_wait_started(&data[i].spin);
	}

	for (i = 0; i < num_placements; i++) {
		xe_spin_end(&data[i].spin);
		igt_assert(syncobj_wait(fd, &syncobjs[i], 1, INT64_MAX, 0,
					NULL));
	}
	igt_assert(syncobj_wait(fd, &sync[0].handle, 1, INT64_MAX, 0, NULL));

	sync[0].flags |= DRM_XE_SYNC_FLAG_SIGNAL;
	xe_vm_unbind_async(fd, vm, 0, 0, addr, bo_size, sync, 1);
	igt_assert(syncobj_wait(fd, &sync[0].handle, 1, INT64_MAX, 0, NULL));

	syncobj_destroy(fd, sync[0].handle);
	for (i = 0; i < num_placements; i++) {
		syncobj_destroy(fd, syncobjs[i]);
		xe_exec_queue_destroy(fd, exec_queues[i]);
	}

	munmap(data, bo_size);
	gem_close(fd, bo);
	xe_vm_destroy(fd, vm);
}

#define MAX_N_EXEC_QUEUES	16
#define USERPTR				(0x1 << 0)
#define REBIND		(0x1 << 1)
#define INVALIDATE	(0x1 << 2)
#define RACE		(0x1 << 3)
#define VIRTUAL		(0x1 << 4)
#define PARALLEL	(0x1 << 5)

/**
 * SUBTEST: once-%s
 * Description: Run %arg[1] test only once
 * Test category: functionality test
 *
 * SUBTEST: many-%s
 * Description: Run %arg[1] test many times
 * Test category: stress test
 *
 * SUBTEST: many-execqueues-%s
 * Description: Run %arg[1] test on many exec_queues
 * Test category: stress test
 *
 * SUBTEST: twice-%s
 * Description: Run %arg[1] test twice
 * Test category: functionality test
 *
 * SUBTEST: no-exec-%s
 * Description: Run no-exec %arg[1] test
 * Test category: functionality test
 *
 * arg[1]:
 *
 * @virtual-basic:			virtual basic
 * @virtual-userptr:			virtual userptr
 * @virtual-rebind:			virtual rebind
 * @virtual-userptr-rebind:		virtual userptr -rebind
 * @virtual-userptr-invalidate:		virtual userptr invalidate
 * @virtual-userptr-invalidate-race:	virtual userptr invalidate racy
 * @parallel-basic:			parallel basic
 * @parallel-userptr:			parallel userptr
 * @parallel-rebind:			parallel rebind
 * @parallel-userptr-rebind:		parallel userptr rebind
 * @parallel-userptr-invalidate:	parallel userptr invalidate
 * @parallel-userptr-invalidate-race:	parallel userptr invalidate racy
 */
static void
test_exec(int fd, int gt, int class, int n_exec_queues, int n_execs,
	  unsigned int flags)
{
	uint32_t vm;
	uint64_t addr = 0x1a0000;
	struct drm_xe_sync sync[2] = {
		{ .type = DRM_XE_SYNC_TYPE_SYNCOBJ, .flags = DRM_XE_SYNC_FLAG_SIGNAL, },
		{ .type = DRM_XE_SYNC_TYPE_SYNCOBJ, .flags = DRM_XE_SYNC_FLAG_SIGNAL, },
	};
	struct drm_xe_exec exec = {
		.num_syncs = 2,
		.syncs = to_user_pointer(sync),
	};
	uint32_t exec_queues[MAX_N_EXEC_QUEUES];
	uint32_t syncobjs[MAX_N_EXEC_QUEUES];
	size_t bo_size;
	uint32_t bo = 0;
	struct {
		uint32_t batch[16];
		uint64_t pad;
		uint32_t data;
	} *data;
	struct drm_xe_engine_class_instance *hwe;
	struct drm_xe_engine_class_instance eci[MAX_INSTANCE];
	int i, j, b, num_placements = 0;

	igt_assert(n_exec_queues <= MAX_N_EXEC_QUEUES);

	xe_for_each_engine(fd, hwe) {
		if (hwe->engine_class != class || hwe->gt_id != gt)
			continue;

		eci[num_placements++] = *hwe;
	}
	if (num_placements < 2)
		return;

	vm = xe_vm_create(fd, 0, 0);
	bo_size = sizeof(*data) * n_execs;
	bo_size = ALIGN(bo_size + xe_cs_prefetch_size(fd), xe_get_default_alignment(fd));

	if (flags & USERPTR) {
#define	MAP_ADDRESS	0x00007fadeadbe000
		if (flags & INVALIDATE) {
			data = mmap((void *)MAP_ADDRESS, bo_size, PROT_READ |
				    PROT_WRITE, MAP_SHARED | MAP_FIXED |
				    MAP_ANONYMOUS, -1, 0);
			igt_assert(data != MAP_FAILED);
		} else {
			data = aligned_alloc(xe_get_default_alignment(fd), bo_size);
			igt_assert(data);
		}
		memset(data, 0, bo_size);
	} else {
		bo = xe_bo_create(fd, vm, bo_size, vram_if_possible(fd, gt),
				  DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM);
		data = xe_bo_map(fd, bo, bo_size);
	}

	for (i = 0; i < n_exec_queues; i++) {
		struct drm_xe_exec_queue_create create = {
			.vm_id = vm,
			.width = flags & PARALLEL ? num_placements : 1,
			.num_placements = flags & PARALLEL ? 1 : num_placements,
			.instances = to_user_pointer(eci),
		};

		igt_assert_eq(igt_ioctl(fd, DRM_IOCTL_XE_EXEC_QUEUE_CREATE,
					&create), 0);
		exec_queues[i] = create.exec_queue_id;
		syncobjs[i] = syncobj_create(fd, 0);
	};
	exec.num_batch_buffer = flags & PARALLEL ? num_placements : 1;

	sync[0].handle = syncobj_create(fd, 0);
	if (bo)
		xe_vm_bind_async(fd, vm, 0, bo, 0, addr, bo_size, sync, 1);
	else
		xe_vm_bind_userptr_async(fd, vm, 0, to_user_pointer(data), addr,
					 bo_size, sync, 1);

	for (i = 0; i < n_execs; i++) {
		uint64_t batch_offset = (char *)&data[i].batch - (char *)data;
		uint64_t batch_addr = addr + batch_offset;
		uint64_t sdi_offset = (char *)&data[i].data - (char *)data;
		uint64_t sdi_addr = addr + sdi_offset;
		uint64_t batches[MAX_INSTANCE];
		int e = i % n_exec_queues;

		for (j = 0; j < num_placements && flags & PARALLEL; ++j)
			batches[j] = batch_addr;

		b = 0;
		data[i].batch[b++] = MI_STORE_DWORD_IMM_GEN4;
		data[i].batch[b++] = sdi_addr;
		data[i].batch[b++] = sdi_addr >> 32;
		data[i].batch[b++] = 0xc0ffee;
		data[i].batch[b++] = MI_BATCH_BUFFER_END;
		igt_assert(b <= ARRAY_SIZE(data[i].batch));

		sync[0].flags &= ~DRM_XE_SYNC_FLAG_SIGNAL;
		sync[1].flags |= DRM_XE_SYNC_FLAG_SIGNAL;
		sync[1].handle = syncobjs[e];

		exec.exec_queue_id = exec_queues[e];
		exec.address = flags & PARALLEL ?
			to_user_pointer(batches) : batch_addr;
		if (e != i)
			 syncobj_reset(fd, &syncobjs[e], 1);
		xe_exec(fd, &exec);

		if (flags & REBIND && i + 1 != n_execs) {
			sync[1].flags &= ~DRM_XE_SYNC_FLAG_SIGNAL;
			xe_vm_unbind_async(fd, vm, 0, 0, addr, bo_size,
					   sync + 1, 1);

			sync[0].flags |= DRM_XE_SYNC_FLAG_SIGNAL;
			addr += bo_size;
			if (bo)
				xe_vm_bind_async(fd, vm, 0, bo, 0, addr,
						 bo_size, sync, 1);
			else
				xe_vm_bind_userptr_async(fd, vm, 0,
							 to_user_pointer(data),
							 addr, bo_size, sync,
							 1);
		}

		if (flags & INVALIDATE && i + 1 != n_execs) {
			if (!(flags & RACE)) {
				/*
				 * Wait for exec completion and check data as
				 * userptr will likely change to different
				 * physical memory on next mmap call triggering
				 * an invalidate.
				 */
				igt_assert(syncobj_wait(fd, &syncobjs[e], 1,
							INT64_MAX, 0, NULL));
				igt_assert_eq(data[i].data, 0xc0ffee);
			} else if (i * 2 != n_execs) {
				/*
				 * We issue 1 mmap which races against running
				 * jobs. No real check here aside from this test
				 * not faulting on the GPU.
				 */
				continue;
			}

			data = mmap((void *)MAP_ADDRESS, bo_size, PROT_READ |
				    PROT_WRITE, MAP_SHARED | MAP_FIXED |
				    MAP_ANONYMOUS, -1, 0);
			igt_assert(data != MAP_FAILED);
		}
	}

	for (i = 0; i < n_exec_queues && n_execs; i++)
		igt_assert(syncobj_wait(fd, &syncobjs[i], 1, INT64_MAX, 0,
					NULL));
	igt_assert(syncobj_wait(fd, &sync[0].handle, 1, INT64_MAX, 0, NULL));

	sync[0].flags |= DRM_XE_SYNC_FLAG_SIGNAL;
	xe_vm_unbind_async(fd, vm, 0, 0, addr, bo_size, sync, 1);
	igt_assert(syncobj_wait(fd, &sync[0].handle, 1, INT64_MAX, 0, NULL));

	for (i = (flags & INVALIDATE && n_execs) ? n_execs - 1 : 0;
	     i < n_execs; i++)
		igt_assert_eq(data[i].data, 0xc0ffee);

	syncobj_destroy(fd, sync[0].handle);
	for (i = 0; i < n_exec_queues; i++) {
		syncobj_destroy(fd, syncobjs[i]);
		xe_exec_queue_destroy(fd, exec_queues[i]);
	}

	if (bo) {
		munmap(data, bo_size);
		gem_close(fd, bo);
	} else if (!(flags & INVALIDATE)) {
		free(data);
	}
	xe_vm_destroy(fd, vm);
}

/**
 * SUBTEST: once-cm-%s
 * Description: Run compute mode virtual exec_queue arg[1] test only once
 * Test category: functionality test
 *
 *
 * SUBTEST: twice-cm-%s
 * Description: Run compute mode virtual exec_queue arg[1] test twice
 * Test category: functionality test
 *
 * SUBTEST: many-cm-%s
 * Description: Run compute mode virtual exec_queue arg[1] test many times
 * Test category: stress test
 *
 * SUBTEST: many-execqueues-cm-%s
 * Description: Run compute mode virtual exec_queue arg[1] test on many exec_queues
 * Test category: stress test
 *
 *
 * SUBTEST: no-exec-cm-%s
 * Description: Run compute mode virtual exec_queue arg[1] no-exec test
 * Test category: functionality test
 *
 * arg[1]:
 *
 * @virtual-basic:			virtual basic
 * @virtual-userptr:			virtual userptr
 * @virtual-rebind:			virtual rebind
 * @virtual-userptr-rebind:		virtual userptr rebind
 * @virtual-userptr-invalidate:		virtual userptr invalidate
 * @virtual-userptr-invalidate-race:	virtual userptr invalidate racy
 * @parallel-basic:			parallel basic
 * @parallel-userptr:			parallel userptr
 * @parallel-rebind:			parallel rebind
 * @parallel-userptr-rebind:		parallel userptr rebind
 * @parallel-userptr-invalidate:	parallel userptr invalidate
 * @parallel-userptr-invalidate-race:	parallel userptr invalidate racy
 */

static void
test_cm(int fd, int gt, int class, int n_exec_queues, int n_execs,
	unsigned int flags)
{
	uint32_t vm;
	uint64_t addr = 0x1a0000;
#define USER_FENCE_VALUE	0xdeadbeefdeadbeefull
	struct drm_xe_sync sync[1] = {
		{ .type = DRM_XE_SYNC_TYPE_USER_FENCE, .flags = DRM_XE_SYNC_FLAG_SIGNAL,
	          .timeline_value = USER_FENCE_VALUE },
	};
	struct drm_xe_exec exec = {
		.num_batch_buffer = 1,
		.num_syncs = 1,
		.syncs = to_user_pointer(sync),
	};
	uint32_t exec_queues[MAX_N_EXEC_QUEUES];
	size_t bo_size;
	uint32_t bo = 0;
	struct {
		uint32_t batch[16];
		uint64_t pad;
		uint64_t vm_sync;
		uint64_t exec_sync;
		uint32_t data;
	} *data;
	struct drm_xe_engine_class_instance *hwe;
	struct drm_xe_engine_class_instance eci[MAX_INSTANCE];
	int i, j, b, num_placements = 0;
	int map_fd = -1;

	igt_assert(n_exec_queues <= MAX_N_EXEC_QUEUES);

	xe_for_each_engine(fd, hwe) {
		if (hwe->engine_class != class || hwe->gt_id != gt)
			continue;

		eci[num_placements++] = *hwe;
	}
	if (num_placements < 2)
		return;

	vm = xe_vm_create(fd, DRM_XE_VM_CREATE_FLAG_LR_MODE, 0);
	bo_size = sizeof(*data) * n_execs;
	bo_size = ALIGN(bo_size + xe_cs_prefetch_size(fd),
			xe_get_default_alignment(fd));

	if (flags & USERPTR) {
#define	MAP_ADDRESS	0x00007fadeadbe000
		if (flags & INVALIDATE) {
			data = mmap((void *)MAP_ADDRESS, bo_size, PROT_READ |
				    PROT_WRITE, MAP_SHARED | MAP_FIXED |
				    MAP_ANONYMOUS, -1, 0);
			igt_assert(data != MAP_FAILED);
		} else {
			data = aligned_alloc(xe_get_default_alignment(fd),
					     bo_size);
			igt_assert(data);
		}
	} else {
		bo = xe_bo_create(fd, vm, bo_size, vram_if_possible(fd, gt),
				  DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM);
		data = xe_bo_map(fd, bo, bo_size);
	}
	memset(data, 0, bo_size);

	for (i = 0; i < n_exec_queues; i++) {
		struct drm_xe_exec_queue_create create = {
			.vm_id = vm,
			.width = flags & PARALLEL ? num_placements : 1,
			.num_placements = flags & PARALLEL ? 1 : num_placements,
			.instances = to_user_pointer(eci),
			.extensions = 0,
		};

		igt_assert_eq(igt_ioctl(fd, DRM_IOCTL_XE_EXEC_QUEUE_CREATE,
					&create), 0);
		exec_queues[i] = create.exec_queue_id;
	}
	exec.num_batch_buffer = flags & PARALLEL ? num_placements : 1;

	sync[0].addr = to_user_pointer(&data[0].vm_sync);
	if (bo)
		xe_vm_bind_async(fd, vm, 0, bo, 0, addr, bo_size, sync, 1);
	else
		xe_vm_bind_userptr_async(fd, vm, 0, to_user_pointer(data), addr,
					 bo_size, sync, 1);

#define ONE_SEC	MS_TO_NS(1000)
	xe_wait_ufence(fd, &data[0].vm_sync, USER_FENCE_VALUE, 0, ONE_SEC);
	data[0].vm_sync = 0;

	for (i = 0; i < n_execs; i++) {
		uint64_t batch_offset = (char *)&data[i].batch - (char *)data;
		uint64_t batch_addr = addr + batch_offset;
		uint64_t sdi_offset = (char *)&data[i].data - (char *)data;
		uint64_t sdi_addr = addr + sdi_offset;
		uint64_t batches[MAX_INSTANCE];
		int e = i % n_exec_queues;

		for (j = 0; j < num_placements && flags & PARALLEL; ++j)
			batches[j] = batch_addr;

		b = 0;
		data[i].batch[b++] = MI_STORE_DWORD_IMM_GEN4;
		data[i].batch[b++] = sdi_addr;
		data[i].batch[b++] = sdi_addr >> 32;
		data[i].batch[b++] = 0xc0ffee;
		data[i].batch[b++] = MI_BATCH_BUFFER_END;
		igt_assert(b <= ARRAY_SIZE(data[i].batch));

		sync[0].addr = addr + (char *)&data[i].exec_sync - (char *)data;

		exec.exec_queue_id = exec_queues[e];
		exec.address = flags & PARALLEL ?
			to_user_pointer(batches) : batch_addr;
		xe_exec(fd, &exec);

		if (flags & REBIND && i + 1 != n_execs) {
			xe_wait_ufence(fd, &data[i].exec_sync, USER_FENCE_VALUE,
				       exec_queues[e], ONE_SEC);
			xe_vm_unbind_async(fd, vm, 0, 0, addr, bo_size, NULL,
					   0);

			sync[0].addr = to_user_pointer(&data[0].vm_sync);
			addr += bo_size;
			if (bo)
				xe_vm_bind_async(fd, vm, 0, bo, 0, addr,
						 bo_size, sync, 1);
			else
				xe_vm_bind_userptr_async(fd, vm, 0,
							 to_user_pointer(data),
							 addr, bo_size, sync,
							 1);
			xe_wait_ufence(fd, &data[0].vm_sync, USER_FENCE_VALUE,
				       0, ONE_SEC);
			data[0].vm_sync = 0;
		}

		if (flags & INVALIDATE && i + 1 != n_execs) {
			if (!(flags & RACE)) {
				/*
				 * Wait for exec completion and check data as
				 * userptr will likely change to different
				 * physical memory on next mmap call triggering
				 * an invalidate.
				 */
				xe_wait_ufence(fd, &data[i].exec_sync,
					       USER_FENCE_VALUE, exec_queues[e],
					       ONE_SEC);
				igt_assert_eq(data[i].data, 0xc0ffee);
			} else if (i * 2 != n_execs) {
				/*
				 * We issue 1 mmap which races against running
				 * jobs. No real check here aside from this test
				 * not faulting on the GPU.
				 */
				continue;
			}

			if (flags & RACE) {
				map_fd = open("/tmp", O_TMPFILE | O_RDWR,
					      0x666);
				write(map_fd, data, bo_size);
				data = mmap((void *)MAP_ADDRESS, bo_size,
					    PROT_READ | PROT_WRITE, MAP_SHARED |
					    MAP_FIXED, map_fd, 0);
			} else {
				data = mmap((void *)MAP_ADDRESS, bo_size,
					    PROT_READ | PROT_WRITE, MAP_SHARED |
					    MAP_FIXED | MAP_ANONYMOUS, -1, 0);
			}
			igt_assert(data != MAP_FAILED);
		}
	}

	j = flags & INVALIDATE && n_execs ? n_execs - 1 : 0;
	for (i = j; i < n_execs; i++)
		xe_wait_ufence(fd, &data[i].exec_sync, USER_FENCE_VALUE,
			       exec_queues[i % n_exec_queues], ONE_SEC);

	/* Wait for all execs to complete */
	if (flags & INVALIDATE)
		usleep(250000);

	sync[0].addr = to_user_pointer(&data[0].vm_sync);
	xe_vm_unbind_async(fd, vm, 0, 0, addr, bo_size, sync, 1);
	xe_wait_ufence(fd, &data[0].vm_sync, USER_FENCE_VALUE, 0, ONE_SEC);

	for (i = (flags & INVALIDATE && n_execs) ? n_execs - 1 : 0;
	     i < n_execs; i++)
		igt_assert_eq(data[i].data, 0xc0ffee);

	for (i = 0; i < n_exec_queues; i++)
		xe_exec_queue_destroy(fd, exec_queues[i]);

	if (bo) {
		munmap(data, bo_size);
		gem_close(fd, bo);
	} else if (!(flags & INVALIDATE)) {
		free(data);
	}
	xe_vm_destroy(fd, vm);
}


igt_main
{
	const struct section {
		const char *name;
		unsigned int flags;
	} sections[] = {
		{ "virtual-basic", VIRTUAL },
		{ "virtual-userptr", VIRTUAL | USERPTR },
		{ "virtual-rebind", VIRTUAL | REBIND },
		{ "virtual-userptr-rebind", VIRTUAL | USERPTR | REBIND },
		{ "virtual-userptr-invalidate", VIRTUAL | USERPTR |
			INVALIDATE },
		{ "virtual-userptr-invalidate-race", VIRTUAL | USERPTR |
			INVALIDATE | RACE },
		{ "parallel-basic", PARALLEL },
		{ "parallel-userptr", PARALLEL | USERPTR },
		{ "parallel-rebind", PARALLEL | REBIND },
		{ "parallel-userptr-rebind", PARALLEL | USERPTR | REBIND },
		{ "parallel-userptr-invalidate", PARALLEL | USERPTR |
			INVALIDATE },
		{ "parallel-userptr-invalidate-race", PARALLEL | USERPTR |
			INVALIDATE | RACE },
		{ NULL },
	};
	int gt;
	int class;
	int fd;

	igt_fixture
		fd = drm_open_driver(DRIVER_XE);

	igt_subtest("virtual-all-active")
		xe_for_each_gt(fd, gt)
			xe_for_each_engine_class(class)
				test_all_active(fd, gt, class);

	for (const struct section *s = sections; s->name; s++) {
		igt_subtest_f("once-%s", s->name)
			xe_for_each_gt(fd, gt)
				xe_for_each_engine_class(class)
					test_exec(fd, gt, class, 1, 1,
						  s->flags);

		igt_subtest_f("twice-%s", s->name)
			xe_for_each_gt(fd, gt)
				xe_for_each_engine_class(class)
					test_exec(fd, gt, class, 1, 2,
						  s->flags);

		igt_subtest_f("many-%s", s->name)
			xe_for_each_gt(fd, gt)
				xe_for_each_engine_class(class)
					test_exec(fd, gt, class, 1,
						  s->flags & (REBIND | INVALIDATE) ?
						  64 : 1024,
						  s->flags);

		igt_subtest_f("many-execqueues-%s", s->name)
			xe_for_each_gt(fd, gt)
				xe_for_each_engine_class(class)
					test_exec(fd, gt, class, 16,
						  s->flags & (REBIND | INVALIDATE) ?
						  64 : 1024,
						  s->flags);

		igt_subtest_f("no-exec-%s", s->name)
			xe_for_each_gt(fd, gt)
				xe_for_each_engine_class(class)
					test_exec(fd, gt, class, 1, 0,
						  s->flags);

		igt_subtest_f("once-cm-%s", s->name)
			xe_for_each_gt(fd, gt)
				xe_for_each_engine_class(class)
					test_cm(fd, gt, class, 1, 1, s->flags);

		igt_subtest_f("twice-cm-%s", s->name)
			xe_for_each_gt(fd, gt)
				xe_for_each_engine_class(class)
					test_cm(fd, gt, class, 1, 2, s->flags);

		igt_subtest_f("many-cm-%s", s->name)
			xe_for_each_gt(fd, gt)
				xe_for_each_engine_class(class)
					test_cm(fd, gt, class, 1,
						s->flags & (REBIND | INVALIDATE) ?
						64 : 1024,
						s->flags);

		igt_subtest_f("many-execqueues-cm-%s", s->name)
			xe_for_each_gt(fd, gt)
				xe_for_each_engine_class(class)
					test_cm(fd, gt, class, 16,
						s->flags & (REBIND | INVALIDATE) ?
						64 : 1024,
						s->flags);

		igt_subtest_f("no-exec-cm-%s", s->name)
			xe_for_each_gt(fd, gt)
				xe_for_each_engine_class(class)
					test_cm(fd, gt, class, 1, 0, s->flags);
	}

	igt_fixture
		drm_close_driver(fd);
}
