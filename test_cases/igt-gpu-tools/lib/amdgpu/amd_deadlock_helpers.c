// SPDX-License-Identifier: MIT
/*
 * Copyright 2022 Advanced Micro Devices, Inc.
 */

#include <amdgpu.h>
#include "amdgpu_drm.h"
#include "amd_PM4.h"
#include "amd_sdma.h"
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include "amd_memory.h"
#include "amd_deadlock_helpers.h"
#include "amd_ip_blocks.h"
#include "lib/amdgpu/amd_command_submission.h"

#define MAX_JOB_COUNT 200

#define MEMORY_OFFSET 256 /* wait for this memory to change */
struct thread_param {
	sigset_t set_ready; /* thread is ready and signal to change memory */
	pthread_t main_thread;
	uint32_t *ib_result_cpu;
};

static int
use_uc_mtype = 1;

static void*
write_mem_address(void *data)
{
	int sig, r;
	struct thread_param *param = data;

	/* send ready signal to main thread */
	pthread_kill(param->main_thread, SIGUSR1);

	/* wait until job is submitted */
	r = sigwait(&param->set_ready, &sig);
	igt_assert_eq(r, 0);
	igt_assert_eq(sig, SIGUSR2);
	param->ib_result_cpu[MEMORY_OFFSET] = 0x1;
	return 0;
}

void
amdgpu_wait_memory_helper(amdgpu_device_handle device_handle, unsigned int ip_type)
{
	amdgpu_context_handle context_handle;
	amdgpu_bo_handle ib_result_handle;
	void *ib_result_cpu;
	uint32_t *ib_result_cpu2;
	uint64_t ib_result_mc_address;
	struct amdgpu_cs_request ibs_request;
	struct amdgpu_cs_ib_info ib_info;
	struct amdgpu_cs_fence fence_status;
	uint32_t expired;
	int r;
	amdgpu_bo_list_handle bo_list;
	amdgpu_va_handle va_handle;
	int bo_cmd_size = 4096;
	int sig = 0;
	pthread_t stress_thread = {0};
	struct thread_param param = {0};
	int job_count = 0;
	struct amdgpu_cmd_base *base_cmd = get_cmd_base();

	r = amdgpu_cs_ctx_create(device_handle, &context_handle);
	igt_assert_eq(r, 0);

	r = amdgpu_bo_alloc_and_map_raw(device_handle, bo_cmd_size, bo_cmd_size,
			AMDGPU_GEM_DOMAIN_GTT, 0, use_uc_mtype ? AMDGPU_VM_MTYPE_UC : 0,
						    &ib_result_handle, &ib_result_cpu,
						    &ib_result_mc_address, &va_handle);
	igt_assert_eq(r, 0);

	r = amdgpu_get_bo_list(device_handle, ib_result_handle, NULL, &bo_list);
	igt_assert_eq(r, 0);

	base_cmd->attach_buf(base_cmd, ib_result_cpu, bo_cmd_size);

	if (ip_type == AMDGPU_HW_IP_DMA) {
		base_cmd->emit(base_cmd, SDMA_PKT_HEADER_OP(SDMA_OP_POLL_REGMEM) |
					(0 << 26) | /* WAIT_REG_MEM */(4 << 28) | /* != */(1 << 31)
					/* memory */);
	} else {
		base_cmd->emit(base_cmd, PACKET3(PACKET3_WAIT_REG_MEM, 5));
		base_cmd->emit(base_cmd, (WAIT_REG_MEM_MEM_SPACE(1)  /* memory */|
							  WAIT_REG_MEM_FUNCTION(4) /* != */|
							  WAIT_REG_MEM_ENGINE(0)/* me */));
	}

	base_cmd->emit(base_cmd, (ib_result_mc_address + MEMORY_OFFSET * 4) & 0xfffffffc);
	base_cmd->emit(base_cmd, ((ib_result_mc_address + MEMORY_OFFSET * 4) >> 32) & 0xffffffff);

	base_cmd->emit(base_cmd, 0);/* reference value */
	base_cmd->emit(base_cmd, 0xffffffff); /* and mask */
	base_cmd->emit(base_cmd, 0x00000004);/* poll interval */
	base_cmd->emit_repeat(base_cmd, GFX_COMPUTE_NOP, 16 - base_cmd->cdw);

	ib_result_cpu2 = ib_result_cpu;
	ib_result_cpu2[MEMORY_OFFSET] = 0x0; /* the memory we wait on to change */

	memset(&ib_info, 0, sizeof(struct amdgpu_cs_ib_info));
	ib_info.ib_mc_address = ib_result_mc_address;
	ib_info.size = base_cmd->cdw;

	memset(&ibs_request, 0, sizeof(struct amdgpu_cs_request));
	ibs_request.ip_type = ip_type;
	ibs_request.ring = 0;
	ibs_request.number_of_ibs = 1;
	ibs_request.ibs = &ib_info;
	ibs_request.resources = bo_list;
	ibs_request.fence_info.handle = NULL;

	/* setup thread parameters and signals of readiness */
	sigemptyset(&param.set_ready);
	sigaddset(&param.set_ready, SIGUSR1);
	sigaddset(&param.set_ready, SIGUSR2);
	r = pthread_sigmask(SIG_BLOCK, &param.set_ready, NULL);
	param.ib_result_cpu = ib_result_cpu;
	param.main_thread = pthread_self();

	r = pthread_create(&stress_thread, NULL, &write_mem_address, &param);
	igt_assert_eq(r, 0);

	/* wait until thread is ready */
	r = sigwait(&param.set_ready, &sig);
	igt_assert_eq(r, 0);
	igt_assert_eq(sig, SIGUSR1);
	/* thread is ready, now submit jobs */
	do {
		/* kernel error failed to initialize parse */
		/* GPU hung is detected becouse we wait for register value*/
		/* submit jobs until it is cancelled , it is about 33 jobs for gfx */
		/* before GPU hung */
		r = amdgpu_cs_submit(context_handle, 0, &ibs_request, 1);
		job_count++;
	} while (r == 0 && job_count < MAX_JOB_COUNT);

	if (r != 0 && r != -ECANCELED)
		igt_assert(0);



	memset(&fence_status, 0, sizeof(struct amdgpu_cs_fence));
	fence_status.context = context_handle;
	fence_status.ip_type = ip_type;
	fence_status.ip_instance = 0;
	fence_status.ring = 0;
	fence_status.fence = ibs_request.seq_no;

	r = amdgpu_cs_query_fence_status(&fence_status, AMDGPU_TIMEOUT_INFINITE, 0,
			&expired);
	if (r != 0 && r != -ECANCELED)
		igt_assert(0);

	/* send signal to modify the memory we wait for */
	pthread_kill(stress_thread, SIGUSR2);

	pthread_join(stress_thread, NULL);

	amdgpu_bo_list_destroy(bo_list);

	amdgpu_bo_unmap_and_free(ib_result_handle, va_handle,
							 ib_result_mc_address, 4096);

	amdgpu_cs_ctx_free(context_handle);
	free_cmd_base(base_cmd);
}

void
bad_access_helper(amdgpu_device_handle device_handle, int reg_access, unsigned int ip_type)
{
	amdgpu_context_handle context_handle;
	amdgpu_bo_handle ib_result_handle;
	void *ib_result_cpu;
	uint64_t ib_result_mc_address;
	struct amdgpu_cs_request ibs_request;
	struct amdgpu_cs_ib_info ib_info;
	struct amdgpu_cs_fence fence_status;
	uint32_t expired;
	const unsigned int bo_cmd_size = 4096;
	const unsigned int alignment = 4096;
	int r;
	amdgpu_bo_list_handle bo_list;
	amdgpu_va_handle va_handle;
	struct amdgpu_cmd_base *base_cmd;

	r = amdgpu_cs_ctx_create(device_handle, &context_handle);
	igt_assert_eq(r, 0);

	r = amdgpu_bo_alloc_and_map_raw(device_handle, bo_cmd_size, alignment,
									AMDGPU_GEM_DOMAIN_GTT, 0, 0,
									&ib_result_handle, &ib_result_cpu,
									&ib_result_mc_address, &va_handle);
	igt_assert_eq(r, 0);
	base_cmd = get_cmd_base();
	base_cmd->attach_buf(base_cmd, ib_result_cpu, bo_cmd_size);

	r = amdgpu_get_bo_list(device_handle, ib_result_handle, NULL, &bo_list);
	igt_assert_eq(r, 0);

	base_cmd->emit(base_cmd, PACKET3(PACKET3_WRITE_DATA, 3));
	base_cmd->emit(base_cmd, (reg_access ? WRITE_DATA_DST_SEL(0) :
										   WRITE_DATA_DST_SEL(5)) | WR_CONFIRM);

	base_cmd->emit(base_cmd, reg_access ? mmVM_CONTEXT0_PAGE_TABLE_BASE_ADDR :
					0xdeadbee0);
	base_cmd->emit(base_cmd, 0);
	base_cmd->emit(base_cmd, 0xdeadbeef);
	base_cmd->emit_repeat(base_cmd, GFX_COMPUTE_NOP, 16 - base_cmd->cdw);

	memset(&ib_info, 0, sizeof(struct amdgpu_cs_ib_info));
	ib_info.ib_mc_address = ib_result_mc_address;
	ib_info.size = base_cmd->cdw;

	memset(&ibs_request, 0, sizeof(struct amdgpu_cs_request));
	ibs_request.ip_type = ip_type;
	ibs_request.ring = 0;
	ibs_request.number_of_ibs = 1;
	ibs_request.ibs = &ib_info;
	ibs_request.resources = bo_list;
	ibs_request.fence_info.handle = NULL;

	r = amdgpu_cs_submit(context_handle, 0, &ibs_request, 1);
	/* see kernel change */
	/* https://lists.freedesktop.org/archives/amd-gfx/2023-May/092770.html */
	if (r != 0 && r != -ECANCELED && r != -ETIME)
		igt_assert(0);


	memset(&fence_status, 0, sizeof(struct amdgpu_cs_fence));
	fence_status.context = context_handle;
	fence_status.ip_type = ip_type;
	fence_status.ip_instance = 0;
	fence_status.ring = 0;
	fence_status.fence = ibs_request.seq_no;

	r = amdgpu_cs_query_fence_status(&fence_status,
			AMDGPU_TIMEOUT_INFINITE, 0, &expired);
	if (r != 0 && r != -ECANCELED && r != -ETIME)
		igt_assert(0);

	amdgpu_bo_list_destroy(bo_list);
	amdgpu_bo_unmap_and_free(ib_result_handle, va_handle,
					 ib_result_mc_address, 4096);
	free_cmd_base(base_cmd);
	amdgpu_cs_ctx_free(context_handle);
}

#define MAX_DMABUF_COUNT 0x20000
#define MAX_DWORD_COUNT 256

void
amdgpu_hang_sdma_helper(amdgpu_device_handle device_handle, uint8_t hang_type)
{
	int j, r;
	uint32_t *ptr, offset;
	struct amdgpu_ring_context *ring_context;
	struct amdgpu_cmd_base *base_cmd = get_cmd_base();
	const struct amdgpu_ip_block_version *ip_block = get_ip_block(device_handle, AMDGPU_HW_IP_DMA);

	ring_context = calloc(1, sizeof(*ring_context));
	if (hang_type == DMA_CORRUPTED_HEADER_HANG) {
		ring_context->write_length = 4096;
		ring_context->pm4 = calloc(MAX_DWORD_COUNT, sizeof(*ring_context->pm4));
		ring_context->pm4_size = MAX_DWORD_COUNT;
	} else {
		ring_context->write_length = MAX_DWORD_COUNT * 4 * MAX_DMABUF_COUNT;
		ring_context->pm4 = calloc(MAX_DWORD_COUNT * MAX_DMABUF_COUNT, sizeof(*ring_context->pm4));
		ring_context->pm4_size = MAX_DWORD_COUNT * MAX_DMABUF_COUNT;
	}
	ring_context->secure = false;
	ring_context->res_cnt = 2;
	igt_assert(ring_context->pm4);

	r = amdgpu_cs_ctx_create(device_handle, &ring_context->context_handle);
	igt_assert_eq(r, 0);

	r = amdgpu_bo_alloc_and_map(device_handle, ring_context->write_length, 4096,
					AMDGPU_GEM_DOMAIN_GTT, 0,
					&ring_context->bo, (void **)&ring_context->bo_cpu,
					&ring_context->bo_mc, &ring_context->va_handle);
	igt_assert_eq(r, 0);

	/* set bo */
	memset((void *)ring_context->bo_cpu, 0, ring_context->write_length);
	r = amdgpu_bo_alloc_and_map(device_handle,
				    ring_context->write_length, 4096,
				    AMDGPU_GEM_DOMAIN_GTT,
				    0, &ring_context->bo2,
				    (void **)&ring_context->bo2_cpu, &ring_context->bo_mc2,
				    &ring_context->va_handle2);
	igt_assert_eq(r, 0);

	/* set bo2 */
	memset((void *)ring_context->bo2_cpu, 0, ring_context->write_length);
	ring_context->resources[0] = ring_context->bo;
	ring_context->resources[1] = ring_context->bo2;
	base_cmd->attach_buf(base_cmd, ring_context->pm4, ring_context->write_length);

	/* fulfill PM4: with bad copy linear header */
	if (hang_type == DMA_CORRUPTED_HEADER_HANG) {
		ip_block->funcs->copy_linear(ip_block->funcs, ring_context, &ring_context->pm4_dw);
		base_cmd->emit_at_offset(base_cmd, 0x23decd3d, 0);
	} else {
		/* Save initialization pm4 */
		ptr = ring_context->pm4;
		for (j = 1; j < MAX_DMABUF_COUNT; j++) {
			/* copy from buf1 to buf2 */
			ip_block->funcs->copy_linear(ip_block->funcs, ring_context, &ring_context->pm4_dw);
			ring_context->pm4 += ring_context->pm4_dw;
			ip_block->funcs->copy_linear(ip_block->funcs, ring_context, &ring_context->pm4_dw);

			offset = ring_context->pm4_dw * 2 * j;
			/* override  addr of buf1 and buf 2 in order to copy from buf2 to buf1 */
			base_cmd->emit_at_offset(base_cmd, (0xffffffff & ring_context->bo_mc2), (offset - 4));
			base_cmd->emit_at_offset(base_cmd,
					((0xffffffff00000000 & ring_context->bo_mc2) >> 32), (offset - 3));
			base_cmd->emit_at_offset(base_cmd, (0xffffffff & ring_context->bo_mc), (offset - 2));
			base_cmd->emit_at_offset(base_cmd,
					((0xffffffff00000000 & ring_context->bo_mc) >> 32), (offset - 1));
			ring_context->pm4 += ring_context->pm4_dw;
		}
		/* restore pm4 */
		ring_context->pm4 = ptr;
		/* update the total pm4_dw */
		ring_context->pm4_dw = ring_context->pm4_dw * 2 * j;
	}

	amdgpu_test_exec_cs_helper(device_handle, ip_block->type, ring_context, 1);
	amdgpu_bo_unmap_and_free(ring_context->bo, ring_context->va_handle, ring_context->bo_mc,
						 ring_context->write_length);
	amdgpu_bo_unmap_and_free(ring_context->bo2, ring_context->va_handle2, ring_context->bo_mc2,
						 ring_context->write_length);
	/* clean resources */
	free(ring_context->pm4);
	/* end of test */
	//r = amdgpu_cs_ctx_free(context_handle);
	r = amdgpu_cs_ctx_free(ring_context->context_handle);
	igt_assert_eq(r, 0);
	free_cmd_base(base_cmd);
}
