// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

/**
 * TEST: Basic tests for execbuf functionality for virtual and parallel exec_queues
 * Category: Hardware building block
 * Sub-category: execbuf
 * Functionality: fault mode
 * GPU requirements: GPU needs support for DRM_XE_VM_CREATE_FLAG_FAULT_MODE
 */

#include <fcntl.h>

#include "igt.h"
#include "lib/igt_syncobj.h"
#include "lib/intel_reg.h"
#include "xe_drm.h"

#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"
#include <string.h>

#define MAX_N_EXEC_QUEUES	16

#define USERPTR		(0x1 << 0)
#define REBIND		(0x1 << 1)
#define INVALIDATE	(0x1 << 2)
#define RACE		(0x1 << 3)
#define BIND_EXEC_QUEUE	(0x1 << 4)
#define IMMEDIATE	(0x1 << 5)
#define PREFETCH	(0x1 << 6)
#define INVALID_FAULT	(0x1 << 7)

/**
 * SUBTEST: once-%s
 * Description: Run %arg[1] fault mode test only once
 * Test category: functionality test
 *
 * SUBTEST: twice-%s
 * Description: Run %arg[1] fault mode test twice
 * Test category: functionality test
 *
 * SUBTEST: many-%s
 * Description: Run %arg[1] fault mode test many times
 * Test category: stress test
 *
 * SUBTEST: many-execqueues-%s
 * Description: Run %arg[1] fault mode test on many exec_queues
 * Test category: stress test
 *
 * arg[1]:
 *
 * @basic:				basic
 * @userptr:				userptr
 * @rebind:				rebind
 * @userptr-rebind:			userptr rebind
 * @userptr-invalidate:			userptr invalidate
 * @userptr-invalidate-race:		userptr invalidate race
 * @bindexecqueue:				bindexecqueue
 * @bindexecqueue-userptr:			bindexecqueue userptr
 * @bindexecqueue-rebind:			bindexecqueue rebind
 * @bindexecqueue-userptr-rebind:		bindexecqueue userptr rebind
 * @bindexecqueue-userptr-invalidate:
 *					bindexecqueue userptr invalidate
 * @bindexecqueue-userptr-invalidate-race:
 *					bindexecqueue userptr invalidate race
 * @basic-imm:				basic imm
 * @userptr-imm:			userptr imm
 * @rebind-imm:				rebind imm
 * @userptr-rebind-imm:			userptr rebind imm
 * @userptr-invalidate-imm:		userptr invalidate imm
 * @userptr-invalidate-race-imm:	userptr invalidate race imm
 * @bindexecqueue-imm:			bindexecqueue imm
 * @bindexecqueue-userptr-imm:		bindexecqueue userptr imm
 * @bindexecqueue-rebind-imm:		bindexecqueue rebind imm
 * @bindexecqueue-userptr-rebind-imm:
 *					bindexecqueue userptr rebind imm
 * @bindexecqueue-userptr-invalidate-imm:
 *					bindexecqueue userptr invalidate imm
 * @bindexecqueue-userptr-invalidate-race-imm:
 *					bindexecqueue userptr invalidate race imm
 * @basic-prefetch:			basic prefetch
 * @userptr-prefetch:			userptr prefetch
 * @rebind-prefetch:			rebind prefetch
 * @userptr-rebind-prefetch:		userptr rebind prefetch
 * @userptr-invalidate-prefetch:	userptr invalidate prefetch
 * @userptr-invalidate-race-prefetch:	userptr invalidate race prefetch
 * @bindexecqueue-prefetch:		bindexecqueue prefetch
 * @bindexecqueue-userptr-prefetch:	bindexecqueue userptr prefetch
 * @bindexecqueue-rebind-prefetch:		bindexecqueue rebind prefetch
 * @bindexecqueue-userptr-rebind-prefetch:	bindexecqueue userptr rebind prefetch
 * @bindexecqueue-userptr-invalidate-prefetch:
 *					bindexecqueue userptr invalidate prefetch
 * @bindexecqueue-userptr-invalidate-race-prefetch:
 *					bindexecqueue userptr invalidate race prefetch
 * @invalid-fault:			invalid fault
 * @invalid-userptr-fault:		invalid userptr fault
 */

static void
test_exec(int fd, struct drm_xe_engine_class_instance *eci,
	  int n_exec_queues, int n_execs, unsigned int flags)
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
	uint32_t bind_exec_queues[MAX_N_EXEC_QUEUES];
	size_t bo_size;
	uint32_t bo = 0;
	struct {
		uint32_t batch[16];
		uint64_t pad;
		uint64_t vm_sync;
		uint64_t exec_sync;
		uint32_t data;
	} *data;
	int i, j, b;
	int map_fd = -1;

	igt_assert(n_exec_queues <= MAX_N_EXEC_QUEUES);

	vm = xe_vm_create(fd, DRM_XE_VM_CREATE_FLAG_LR_MODE |
			  DRM_XE_VM_CREATE_FLAG_FAULT_MODE, 0);
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
		if (flags & PREFETCH)
			bo = xe_bo_create(fd, 0, bo_size,
					  all_memory_regions(fd) |
					  vram_if_possible(fd, 0),
					  DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM);
		else
			bo = xe_bo_create(fd, 0, bo_size,
					  vram_if_possible(fd, eci->gt_id),
					  DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM);
		data = xe_bo_map(fd, bo, bo_size);
	}
	memset(data, 0, bo_size);

	for (i = 0; i < n_exec_queues; i++) {
		exec_queues[i] = xe_exec_queue_create(fd, vm, eci, 0);
		if (flags & BIND_EXEC_QUEUE)
			bind_exec_queues[i] =
				xe_bind_exec_queue_create(fd, vm, 0);
		else
			bind_exec_queues[i] = 0;
	};

	sync[0].addr = to_user_pointer(&data[0].vm_sync);
	if (flags & IMMEDIATE) {
		if (bo)
			xe_vm_bind_async_flags(fd, vm, bind_exec_queues[0], bo, 0,
					       addr, bo_size, sync, 1,
					       DRM_XE_VM_BIND_FLAG_IMMEDIATE);
		else
			xe_vm_bind_userptr_async_flags(fd, vm, bind_exec_queues[0],
						       to_user_pointer(data),
						       addr, bo_size, sync, 1,
						       DRM_XE_VM_BIND_FLAG_IMMEDIATE);
	} else {
		if (bo)
			xe_vm_bind_async(fd, vm, bind_exec_queues[0], bo, 0, addr,
					 bo_size, sync, 1);
		else
			xe_vm_bind_userptr_async(fd, vm, bind_exec_queues[0],
						 to_user_pointer(data), addr,
						 bo_size, sync, 1);
	}

#define ONE_SEC	MS_TO_NS(1000)
	xe_wait_ufence(fd, &data[0].vm_sync, USER_FENCE_VALUE,
		       bind_exec_queues[0], ONE_SEC);
	data[0].vm_sync = 0;

	if (flags & PREFETCH) {
		/* Should move to system memory */
		xe_vm_prefetch_async(fd, vm, bind_exec_queues[0], 0, addr,
				     bo_size, sync, 1, 0);
		xe_wait_ufence(fd, &data[0].vm_sync, USER_FENCE_VALUE,
			       bind_exec_queues[0], ONE_SEC);
		data[0].vm_sync = 0;
	}

	for (i = 0; i < n_execs; i++) {
		uint64_t batch_offset = (char *)&data[i].batch - (char *)data;
		uint64_t batch_addr = addr + batch_offset;
		uint64_t sdi_offset = (char *)&data[i].data - (char *)data;
		uint64_t sdi_addr = addr + sdi_offset;
		int e = i % n_exec_queues;

		b = 0;
		data[i].batch[b++] = MI_STORE_DWORD_IMM_GEN4;
		data[i].batch[b++] = sdi_addr;
		data[i].batch[b++] = sdi_addr >> 32;
		data[i].batch[b++] = 0xc0ffee;
		data[i].batch[b++] = MI_BATCH_BUFFER_END;
		igt_assert(b <= ARRAY_SIZE(data[i].batch));

		sync[0].addr = addr + (char *)&data[i].exec_sync - (char *)data;

		exec.exec_queue_id = exec_queues[e];
		exec.address = batch_addr;
		xe_exec(fd, &exec);

		if (flags & REBIND && i + 1 != n_execs) {
			xe_wait_ufence(fd, &data[i].exec_sync, USER_FENCE_VALUE,
				       exec_queues[e], ONE_SEC);
			xe_vm_unbind_async(fd, vm, bind_exec_queues[e], 0,
					   addr, bo_size, NULL, 0);

			sync[0].addr = to_user_pointer(&data[0].vm_sync);
			addr += bo_size;
			if (bo)
				xe_vm_bind_async(fd, vm, bind_exec_queues[e], bo,
						 0, addr, bo_size, sync, 1);
			else
				xe_vm_bind_userptr_async(fd, vm,
							 bind_exec_queues[e],
							 to_user_pointer(data),
							 addr, bo_size, sync,
							 1);
			xe_wait_ufence(fd, &data[0].vm_sync, USER_FENCE_VALUE,
				       bind_exec_queues[e], ONE_SEC);
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

	if (!(flags & INVALID_FAULT)) {
		j = flags & INVALIDATE ? n_execs - 1 : 0;
		for (i = j; i < n_execs; i++)
			xe_wait_ufence(fd, &data[i].exec_sync, USER_FENCE_VALUE,
				       exec_queues[i % n_exec_queues], ONE_SEC);
	}

	sync[0].addr = to_user_pointer(&data[0].vm_sync);
	xe_vm_unbind_async(fd, vm, bind_exec_queues[0], 0, addr, bo_size,
			   sync, 1);
	xe_wait_ufence(fd, &data[0].vm_sync, USER_FENCE_VALUE,
		       bind_exec_queues[0], ONE_SEC);

	if (!(flags & INVALID_FAULT)) {
		for (i = j; i < n_execs; i++)
			igt_assert_eq(data[i].data, 0xc0ffee);
	}

	for (i = 0; i < n_exec_queues; i++) {
		xe_exec_queue_destroy(fd, exec_queues[i]);
		if (bind_exec_queues[i])
			xe_exec_queue_destroy(fd, bind_exec_queues[i]);
	}

	if (bo) {
		munmap(data, bo_size);
		gem_close(fd, bo);
	} else if (!(flags & INVALIDATE)) {
		free(data);
	}
	xe_vm_destroy(fd, vm);
	if (map_fd != -1)
		close(map_fd);
}

igt_main
{
	struct drm_xe_engine_class_instance *hwe;
	const struct section {
		const char *name;
		unsigned int flags;
	} sections[] = {
		{ "basic", 0 },
		{ "userptr", USERPTR },
		{ "rebind", REBIND },
		{ "userptr-rebind", USERPTR | REBIND },
		{ "userptr-invalidate", USERPTR | INVALIDATE },
		{ "userptr-invalidate-race", USERPTR | INVALIDATE | RACE },
		{ "bindexecqueue", BIND_EXEC_QUEUE },
		{ "bindexecqueue-userptr", BIND_EXEC_QUEUE | USERPTR },
		{ "bindexecqueue-rebind",  BIND_EXEC_QUEUE | REBIND },
		{ "bindexecqueue-userptr-rebind", BIND_EXEC_QUEUE | USERPTR |
			REBIND },
		{ "bindexecqueue-userptr-invalidate", BIND_EXEC_QUEUE | USERPTR |
			INVALIDATE },
		{ "bindexecqueue-userptr-invalidate-race", BIND_EXEC_QUEUE | USERPTR |
			INVALIDATE | RACE },
		{ "basic-imm", IMMEDIATE },
		{ "userptr-imm", IMMEDIATE | USERPTR },
		{ "rebind-imm", IMMEDIATE | REBIND },
		{ "userptr-rebind-imm", IMMEDIATE | USERPTR | REBIND },
		{ "userptr-invalidate-imm", IMMEDIATE | USERPTR | INVALIDATE },
		{ "userptr-invalidate-race-imm", IMMEDIATE | USERPTR |
			INVALIDATE | RACE },
		{ "bindexecqueue-imm", IMMEDIATE | BIND_EXEC_QUEUE },
		{ "bindexecqueue-userptr-imm", IMMEDIATE | BIND_EXEC_QUEUE | USERPTR },
		{ "bindexecqueue-rebind-imm", IMMEDIATE | BIND_EXEC_QUEUE | REBIND },
		{ "bindexecqueue-userptr-rebind-imm", IMMEDIATE | BIND_EXEC_QUEUE |
			USERPTR | REBIND },
		{ "bindexecqueue-userptr-invalidate-imm", IMMEDIATE | BIND_EXEC_QUEUE |
			USERPTR | INVALIDATE },
		{ "bindexecqueue-userptr-invalidate-race-imm", IMMEDIATE |
			BIND_EXEC_QUEUE | USERPTR | INVALIDATE | RACE },
		{ "basic-prefetch", PREFETCH },
		{ "userptr-prefetch", PREFETCH | USERPTR },
		{ "rebind-prefetch", PREFETCH | REBIND },
		{ "userptr-rebind-prefetch", PREFETCH | USERPTR | REBIND },
		{ "userptr-invalidate-prefetch", PREFETCH | USERPTR | INVALIDATE },
		{ "userptr-invalidate-race-prefetch", PREFETCH | USERPTR |
			INVALIDATE | RACE },
		{ "bindexecqueue-prefetch", PREFETCH | BIND_EXEC_QUEUE },
		{ "bindexecqueue-userptr-prefetch", PREFETCH | BIND_EXEC_QUEUE | USERPTR },
		{ "bindexecqueue-rebind-prefetch", PREFETCH | BIND_EXEC_QUEUE | REBIND },
		{ "bindexecqueue-userptr-rebind-prefetch", PREFETCH | BIND_EXEC_QUEUE |
			USERPTR | REBIND },
		{ "bindexecqueue-userptr-invalidate-prefetch", PREFETCH | BIND_EXEC_QUEUE |
			USERPTR | INVALIDATE },
		{ "bindexecqueue-userptr-invalidate-race-prefetch", PREFETCH |
			BIND_EXEC_QUEUE | USERPTR | INVALIDATE | RACE },
		{ "invalid-fault", INVALID_FAULT },
		{ "invalid-userptr-fault", INVALID_FAULT | USERPTR },
		{ NULL },
	};
	int fd;

	igt_fixture {
		fd = drm_open_driver(DRIVER_XE);
		igt_require(xe_supports_faults(fd));
	}

	for (const struct section *s = sections; s->name; s++) {
		igt_subtest_f("once-%s", s->name)
			xe_for_each_engine(fd, hwe)
				test_exec(fd, hwe, 1, 1, s->flags);

		igt_subtest_f("twice-%s", s->name)
			xe_for_each_engine(fd, hwe)
				test_exec(fd, hwe, 1, 2, s->flags);

		igt_subtest_f("many-%s", s->name)
			xe_for_each_engine(fd, hwe)
				test_exec(fd, hwe, 1,
					  s->flags & (REBIND | INVALIDATE) ?
					  64 : 128,
					  s->flags);

		igt_subtest_f("many-execqueues-%s", s->name)
			xe_for_each_engine(fd, hwe)
				test_exec(fd, hwe, 16,
					  s->flags & (REBIND | INVALIDATE) ?
					  64 : 128,
					  s->flags);
	}

	igt_fixture
		drm_close_driver(fd);
}
