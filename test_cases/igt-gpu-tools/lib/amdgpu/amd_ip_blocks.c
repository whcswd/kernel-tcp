// SPDX-License-Identifier: MIT
/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 * Copyright 2022 Advanced Micro Devices, Inc.
 * Copyright 2023 Advanced Micro Devices, Inc.
 */

#include <fcntl.h>

#include "amd_memory.h"
#include "amd_ip_blocks.h"
#include "amd_PM4.h"
#include "amd_sdma.h"
#include <amdgpu.h>


#include <amdgpu_drm.h>
#include "amdgpu_asic_addr.h"
#include "amd_family.h"
#include "amd_gfx_v8_0.h"
#include "ioctl_wrappers.h"

/*
 * SDMA functions:
 * - write_linear
 * - const_fill
 * - copy_linear
 */
static int
sdma_ring_write_linear(const struct amdgpu_ip_funcs *func,
		       const struct amdgpu_ring_context *ring_context,
		       uint32_t *pm4_dw)
{
	uint32_t i, j;

	i = 0;
	j = 0;
	if (func->family_id == AMDGPU_FAMILY_SI)
		ring_context->pm4[i++] = SDMA_PACKET_SI(SDMA_OPCODE_WRITE, 0, 0, 0,
					 ring_context->write_length);
	else
		ring_context->pm4[i++] = SDMA_PACKET(SDMA_OPCODE_WRITE,
					 SDMA_WRITE_SUB_OPCODE_LINEAR,
					 ring_context->secure ? SDMA_ATOMIC_TMZ(1) : 0);

	ring_context->pm4[i++] = lower_32_bits(ring_context->bo_mc);
	ring_context->pm4[i++] = upper_32_bits(ring_context->bo_mc);
	if (func->family_id >= AMDGPU_FAMILY_AI)
		ring_context->pm4[i++] = ring_context->write_length - 1;
	else
		ring_context->pm4[i++] = ring_context->write_length;

	while (j++ < ring_context->write_length)
		ring_context->pm4[i++] = func->deadbeaf;

	*pm4_dw = i;

	return 0;
}

static int
sdma_ring_atomic(const struct amdgpu_ip_funcs *func,
		       const struct amdgpu_ring_context *ring_context,
		       uint32_t *pm4_dw)
{
	uint32_t i = 0;

	memset(ring_context->pm4, 0, ring_context->pm4_size * sizeof(uint32_t));

		/* atomic opcode for 32b w/ RTN and ATOMIC_SWAPCMP_RTN
		 * loop, 1-loop_until_compare_satisfied.
		 * single_pass_atomic, 0-lru
		 */
	ring_context->pm4[i++] = SDMA_PACKET(SDMA_OPCODE_ATOMIC,
				       0,
				       SDMA_ATOMIC_LOOP(1) |
				       (ring_context->secure ? SDMA_ATOMIC_TMZ(1) : SDMA_ATOMIC_TMZ(0)) |
				       SDMA_ATOMIC_OPCODE(TC_OP_ATOMIC_CMPSWAP_RTN_32));
	ring_context->pm4[i++] = lower_32_bits(ring_context->bo_mc);
	ring_context->pm4[i++] = upper_32_bits(ring_context->bo_mc);
	ring_context->pm4[i++] = 0x12345678;
	ring_context->pm4[i++] = 0x0;
	ring_context->pm4[i++] = func->deadbeaf;
	ring_context->pm4[i++] = 0x0;
	ring_context->pm4[i++] = 0x100;
	*pm4_dw = i;

	return 0;

}

static int
sdma_ring_const_fill(const struct amdgpu_ip_funcs *func,
		     const struct amdgpu_ring_context *context,
		     uint32_t *pm4_dw)
{
	uint32_t i;

	i = 0;
	if (func->family_id == AMDGPU_FAMILY_SI) {
		context->pm4[i++] = SDMA_PACKET_SI(SDMA_OPCODE_CONSTANT_FILL_SI,
						   0, 0, 0, context->write_length / 4);
		context->pm4[i++] = lower_32_bits(context->bo_mc);
		context->pm4[i++] = 0xdeadbeaf;
		context->pm4[i++] = upper_32_bits(context->bo_mc) >> 16;
	} else {
		context->pm4[i++] = SDMA_PACKET(SDMA_OPCODE_CONSTANT_FILL, 0,
						SDMA_CONSTANT_FILL_EXTRA_SIZE(2));
		context->pm4[i++] = lower_32_bits(context->bo_mc);
		context->pm4[i++] = upper_32_bits(context->bo_mc);
		context->pm4[i++] = func->deadbeaf;

		if (func->family_id >= AMDGPU_FAMILY_AI)
			context->pm4[i++] = context->write_length - 1;
		else
			context->pm4[i++] = context->write_length;
	}
	*pm4_dw = i;

	return 0;
}

static int
sdma_ring_copy_linear(const struct amdgpu_ip_funcs *func,
		      const struct amdgpu_ring_context *context,
		      uint32_t *pm4_dw)
{
	uint32_t i;

	i = 0;
	if (func->family_id == AMDGPU_FAMILY_SI) {
		context->pm4[i++] = SDMA_PACKET_SI(SDMA_OPCODE_COPY_SI,
					  0, 0, 0,
					  context->write_length);
		context->pm4[i++] = lower_32_bits(context->bo_mc);
		context->pm4[i++] = upper_32_bits(context->bo_mc);
		context->pm4[i++] = lower_32_bits(context->bo_mc2);
		context->pm4[i++] = upper_32_bits(context->bo_mc2);
	} else {
		context->pm4[i++] = SDMA_PACKET(SDMA_OPCODE_COPY,
				       SDMA_COPY_SUB_OPCODE_LINEAR,
				       0);
		if (func->family_id >= AMDGPU_FAMILY_AI)
			context->pm4[i++] = context->write_length - 1;
		else
			context->pm4[i++] = context->write_length;
		context->pm4[i++] = 0;
		context->pm4[i++] = lower_32_bits(context->bo_mc);
		context->pm4[i++] = upper_32_bits(context->bo_mc);
		context->pm4[i++] = lower_32_bits(context->bo_mc2);
		context->pm4[i++] = upper_32_bits(context->bo_mc2);
	}

	*pm4_dw = i;

	return 0;
}

/*
 * GFX and COMPUTE functions:
 * - write_linear
 * - const_fill
 * - copy_linear
 */


static int
gfx_ring_write_linear(const struct amdgpu_ip_funcs *func,
		      const struct amdgpu_ring_context *ring_context,
		      uint32_t *pm4_dw)
{
	uint32_t i, j;

	i = 0;
	j = 0;

	ring_context->pm4[i++] = PACKET3(PACKET3_WRITE_DATA, 2 +  ring_context->write_length);
	ring_context->pm4[i++] = WRITE_DATA_DST_SEL(5) | WR_CONFIRM;
	ring_context->pm4[i++] = lower_32_bits(ring_context->bo_mc);
	ring_context->pm4[i++] = upper_32_bits(ring_context->bo_mc);
	while (j++ < ring_context->write_length)
		ring_context->pm4[i++] = func->deadbeaf;

	*pm4_dw = i;
	return 0;
}

static int
gfx_ring_atomic(const struct amdgpu_ip_funcs *func,
		      const struct amdgpu_ring_context *ring_context,
		      uint32_t *pm4_dw)
{
	uint32_t i = 0;

	memset(ring_context->pm4, 0, ring_context->pm4_size * sizeof(uint32_t));
		ring_context->pm4[i++] = PACKET3(PACKET3_ATOMIC_MEM, 7);

	/* atomic opcode for 32b w/ RTN and ATOMIC_SWAPCMP_RTN
	 * command, 1-loop_until_compare_satisfied.
	 * single_pass_atomic, 0-lru
	 * engine_sel, 0-micro_engine
	 */
	ring_context->pm4[i++] = (TC_OP_ATOMIC_CMPSWAP_RTN_32 |
				ATOMIC_MEM_COMMAND(1) |
				ATOMIC_MEM_CACHEPOLICAY(0) |
				ATOMIC_MEM_ENGINESEL(0));
	ring_context->pm4[i++] = lower_32_bits(ring_context->bo_mc);
	ring_context->pm4[i++] = upper_32_bits(ring_context->bo_mc);
	ring_context->pm4[i++] = 0x12345678;
	ring_context->pm4[i++] = 0x0;
	ring_context->pm4[i++] = 0xdeadbeaf;
	ring_context->pm4[i++] = 0x0;
	ring_context->pm4[i++] = 0x100;

	*pm4_dw = i;
	return 0;
}

static int
gfx_ring_const_fill(const struct amdgpu_ip_funcs *func,
		     const struct amdgpu_ring_context *ring_context,
		     uint32_t *pm4_dw)
{
	uint32_t i;

	i = 0;
	if (func->family_id == AMDGPU_FAMILY_SI) {
		ring_context->pm4[i++] = PACKET3(PACKET3_DMA_DATA_SI, 4);
		ring_context->pm4[i++] = func->deadbeaf;
		ring_context->pm4[i++] = PACKET3_DMA_DATA_SI_ENGINE(0) |
					 PACKET3_DMA_DATA_SI_DST_SEL(0) |
					 PACKET3_DMA_DATA_SI_SRC_SEL(2) |
					 PACKET3_DMA_DATA_SI_CP_SYNC;
		ring_context->pm4[i++] = lower_32_bits(ring_context->bo_mc);
		ring_context->pm4[i++] = upper_32_bits(ring_context->bo_mc);
		ring_context->pm4[i++] = ring_context->write_length;
	} else {
		ring_context->pm4[i++] = PACKET3(PACKET3_DMA_DATA, 5);
		ring_context->pm4[i++] = PACKET3_DMA_DATA_ENGINE(0) |
					 PACKET3_DMA_DATA_DST_SEL(0) |
					 PACKET3_DMA_DATA_SRC_SEL(2) |
					 PACKET3_DMA_DATA_CP_SYNC;
		ring_context->pm4[i++] = func->deadbeaf;
		ring_context->pm4[i++] = 0;
		ring_context->pm4[i++] = lower_32_bits(ring_context->bo_mc);
		ring_context->pm4[i++] = upper_32_bits(ring_context->bo_mc);
		ring_context->pm4[i++] = ring_context->write_length;
	}
	*pm4_dw = i;

	return 0;
}

static int
gfx_ring_copy_linear(const struct amdgpu_ip_funcs *func,
		     const struct amdgpu_ring_context *context,
		     uint32_t *pm4_dw)
{
	uint32_t i;

	i = 0;
	if (func->family_id == AMDGPU_FAMILY_SI) {
		context->pm4[i++] = PACKET3(PACKET3_DMA_DATA_SI, 4);
		context->pm4[i++] = lower_32_bits(context->bo_mc);
		context->pm4[i++] = PACKET3_DMA_DATA_SI_ENGINE(0) |
			   PACKET3_DMA_DATA_SI_DST_SEL(0) |
			   PACKET3_DMA_DATA_SI_SRC_SEL(0) |
			   PACKET3_DMA_DATA_SI_CP_SYNC |
			   upper_32_bits(context->bo_mc);
		context->pm4[i++] = lower_32_bits(context->bo_mc2);
		context->pm4[i++] = upper_32_bits(context->bo_mc2);
		context->pm4[i++] = context->write_length;
	} else {
		context->pm4[i++] = PACKET3(PACKET3_DMA_DATA, 5);
		context->pm4[i++] = PACKET3_DMA_DATA_ENGINE(0) |
			   PACKET3_DMA_DATA_DST_SEL(0) |
			   PACKET3_DMA_DATA_SRC_SEL(0) |
			   PACKET3_DMA_DATA_CP_SYNC;
		context->pm4[i++] = lower_32_bits(context->bo_mc);
		context->pm4[i++] = upper_32_bits(context->bo_mc);
		context->pm4[i++] = lower_32_bits(context->bo_mc2);
		context->pm4[i++] = upper_32_bits(context->bo_mc2);
		context->pm4[i++] = context->write_length;
	}

	*pm4_dw = i;

	return 0;
}

/* we may cobine these two functions later */
static int
x_compare(const struct amdgpu_ip_funcs *func,
	  const struct amdgpu_ring_context *ring_context, int div)
{
	int i = 0, ret = 0;

	int num_compare = ring_context->write_length/div;

	while (i < num_compare) {
		if (ring_context->bo_cpu[i++] != func->deadbeaf) {
			ret = -1;
			break;
		}
	}
	return ret;
}

static int
x_compare_pattern(const struct amdgpu_ip_funcs *func,
	  const struct amdgpu_ring_context *ring_context, int div)
{
	int i = 0, ret = 0;

	int num_compare = ring_context->write_length/div;

	while (i < num_compare) {
		if (ring_context->bo2_cpu[i++] != func->pattern) {
			ret = -1;
			break;
		}
	}
	return ret;
}

static struct amdgpu_ip_funcs gfx_v8_x_ip_funcs = {
	.family_id = FAMILY_VI,
	.align_mask = 0xff,
	.nop = 0x80000000,
	.deadbeaf = 0xdeadbeaf,
	.pattern = 0xaaaaaaaa,
	.write_linear = gfx_ring_write_linear,
	.write_linear_atomic = gfx_ring_atomic,
	.const_fill = gfx_ring_const_fill,
	.copy_linear = gfx_ring_copy_linear,
	.compare = x_compare,
	.compare_pattern = x_compare_pattern,
	.get_reg_offset = gfx_v8_0_get_reg_offset,
};

static struct amdgpu_ip_funcs sdma_v3_x_ip_funcs = {
	.family_id = FAMILY_VI,
	.align_mask = 0xff,
	.nop = 0x80000000,
	.deadbeaf = 0xdeadbeaf,
	.pattern = 0xaaaaaaaa,
	.write_linear = sdma_ring_write_linear,
	.write_linear_atomic = sdma_ring_atomic,
	.const_fill = sdma_ring_const_fill,
	.copy_linear = sdma_ring_copy_linear,
	.compare = x_compare,
	.compare_pattern = x_compare_pattern,
	.get_reg_offset = gfx_v8_0_get_reg_offset,
};

struct amdgpu_ip_block_version gfx_v8_x_ip_block = {
	.type = AMD_IP_GFX,
	.major = 8,
	.minor = 0,
	.rev = 0,
	.funcs = &gfx_v8_x_ip_funcs
};

struct amdgpu_ip_block_version compute_v8_x_ip_block = {
	.type = AMD_IP_COMPUTE,
	.major = 8,
	.minor = 0,
	.rev = 0,
	.funcs = &gfx_v8_x_ip_funcs
};

struct amdgpu_ip_block_version sdma_v3_x_ip_block = {
	.type = AMD_IP_DMA,
	.major = 3,
	.minor = 0,
	.rev = 0,
	.funcs = &sdma_v3_x_ip_funcs
};

struct chip_info {
	const char *name;
	enum radeon_family family;
	enum chip_class chip_class;
	  amdgpu_device_handle dev;
};

/* we may improve later */
struct amdgpu_ip_blocks_device amdgpu_ips;
struct chip_info g_chip;

static int
amdgpu_device_ip_block_add(struct amdgpu_ip_block_version *ip_block_version)
{
	if (amdgpu_ips.num_ip_blocks >= AMD_IP_MAX)
		return -1;

	amdgpu_ips.ip_blocks[amdgpu_ips.num_ip_blocks++] = ip_block_version;

	return 0;
}

const struct amdgpu_ip_block_version *
get_ip_block(amdgpu_device_handle device, enum amd_ip_block_type type)
{
	int i;

	if (g_chip.dev != device)
		return NULL;

	for (i = 0; i <  amdgpu_ips.num_ip_blocks; i++)
		if (amdgpu_ips.ip_blocks[i]->type == type)
			return amdgpu_ips.ip_blocks[i];
	return NULL;
}

static int
cmd_allocate_buf(struct amdgpu_cmd_base  *base, uint32_t size_dw)
{
	if (size_dw > base->max_dw) {
		if (base->buf) {
			free(base->buf);
			base->buf = NULL;
			base->max_dw = 0;
			base->cdw = 0;
		}
		base->buf = calloc(4, size_dw);
		if (!base->buf)
			return -1;
		base->max_dw = size_dw;
		base->cdw = 0;
	}
	return 0;
}

static int
cmd_attach_buf(struct amdgpu_cmd_base  *base, void *ptr, uint32_t size_bytes)
{
	if (base->buf && base->is_assigned_buf)
		return -1;

	if (base->buf) {
		free(base->buf);
		base->buf = NULL;
		base->max_dw = 0;
		base->cdw = 0;
	}
	assert(ptr != NULL);
	base->buf = (uint32_t *)ptr;
	base->max_dw = size_bytes>>2;
	base->cdw = 0;
	base->is_assigned_buf = true; /* allocated externally , no free */
	return 0;
}

static void
cmd_emit(struct amdgpu_cmd_base  *base, uint32_t value)
{
	assert(base->cdw <  base->max_dw);
	base->buf[base->cdw++] = value;
}

static void
cmd_emit_aligned(struct amdgpu_cmd_base *base, uint32_t mask, uint32_t cmd)
{
	while (base->cdw & mask)
		base->emit(base, cmd);
}
static void
cmd_emit_buf(struct amdgpu_cmd_base  *base, const void *ptr, uint32_t offset_bytes, uint32_t size_bytes)
{
	uint32_t total_offset_dw = (offset_bytes + size_bytes) >> 2;
	uint32_t offset_dw = offset_bytes >> 2;
	/*TODO read the requirements to fix */
	assert(size_bytes % 4 == 0); /* no gaps */
	assert(offset_bytes % 4 == 0);
	assert(base->cdw + total_offset_dw <  base->max_dw);
	memcpy(base->buf + base->cdw + offset_dw, ptr, size_bytes);
	base->cdw += total_offset_dw;
}

static void
cmd_emit_repeat(struct amdgpu_cmd_base  *base, uint32_t value, uint32_t number_of_times)
{
	while (number_of_times > 0) {
		assert(base->cdw <  base->max_dw);
		base->buf[base->cdw++] = value;
		number_of_times--;
	}
}

static void
cmd_emit_at_offset(struct amdgpu_cmd_base  *base, uint32_t value, uint32_t offset_dwords)
{
	assert(base->cdw + offset_dwords <  base->max_dw);
	base->buf[base->cdw + offset_dwords] = value;
}

struct amdgpu_cmd_base *
get_cmd_base(void)
{
	struct amdgpu_cmd_base *base = calloc(1, sizeof(*base));

	base->cdw = 0;
	base->max_dw = 0;
	base->buf = NULL;
	base->is_assigned_buf = false;

	base->allocate_buf = cmd_allocate_buf;
	base->attach_buf = cmd_attach_buf;
	base->emit = cmd_emit;
	base->emit_aligned = cmd_emit_aligned;
	base->emit_repeat = cmd_emit_repeat;
	base->emit_at_offset = cmd_emit_at_offset;
	base->emit_buf = cmd_emit_buf;

	return base;
}

void
free_cmd_base(struct amdgpu_cmd_base *base)
{
	if (base) {
		if (base->buf && base->is_assigned_buf == false)
			free(base->buf);
		free(base);
	}

}

/*
 * GFX: 8.x
 * COMPUTE: 8.x
 * SDMA 3.x
 *
 * GFX9:
 * COMPUTE: 9.x
 * SDMA 4.x
 *
 * GFX10.1:
 * COMPUTE: 10.1
 * SDMA 5.0
 *
 * GFX10.3:
 * COMPUTE: 10.3
 * SDMA 5.2
 *
 * copy function from mesa
 *  should be called once per test
 */
int setup_amdgpu_ip_blocks(uint32_t major, uint32_t minor, struct amdgpu_gpu_info *amdinfo,
			   amdgpu_device_handle device)
{
#define identify_chip2(asic, chipname)	\
	do {\
		if (ASICREV_IS(amdinfo->chip_external_rev, asic)) {\
			info->family = CHIP_##chipname;	\
			info->name = #chipname;	\
		} \
	} while (0)

#define identify_chip(chipname) identify_chip2(chipname, chipname)

	const struct chip_class_arr {
		const char *name;
		enum chip_class class;
	} chip_class_arr[] = {
		{"CLASS_UNKNOWN",	CLASS_UNKNOWN},
		{"R300",				R300},
		{"R400",				R400},
		{"R500",				R500},
		{"R600",				R600},
		{"R700",				R700},
		{"EVERGREEN",			EVERGREEN},
		{"CAYMAN",				CAYMAN},
		{"GFX6",				GFX6},
		{"GFX7",				GFX7},
		{"GFX8",				GFX8},
		{"GFX9",				GFX9},
		{"GFX10",				GFX10},
		{"GFX10_3",				GFX10_3},
		{"GFX11",				GFX11},
		{},
	};
	struct chip_info *info = &g_chip;

	switch (amdinfo->family_id) {
	case AMDGPU_FAMILY_SI:
		identify_chip(TAHITI);
		identify_chip(PITCAIRN);
		identify_chip2(CAPEVERDE, VERDE);
		identify_chip(OLAND);
		identify_chip(HAINAN);
		break;
	case FAMILY_CI:
		identify_chip(BONAIRE);//tested
		identify_chip(HAWAII);
		break;
	case FAMILY_KV:
		identify_chip2(SPECTRE, KAVERI);
		identify_chip2(SPOOKY, KAVERI);
		identify_chip2(KALINDI, KABINI);
		identify_chip2(GODAVARI, KABINI);
		break;
	case FAMILY_VI:
		identify_chip(ICELAND);
		identify_chip(TONGA);
		identify_chip(FIJI);
		identify_chip(POLARIS10);
		identify_chip(POLARIS11);//tested
		identify_chip(POLARIS12);
		identify_chip(VEGAM);
		break;
	case FAMILY_CZ:
		identify_chip(CARRIZO);
		identify_chip(STONEY);
		break;
	case FAMILY_AI:
		identify_chip(VEGA10);
		identify_chip(VEGA12);
		identify_chip(VEGA20);
		identify_chip(ARCTURUS);
		identify_chip(ALDEBARAN);
		break;
	case FAMILY_RV:
		identify_chip(RAVEN);
		identify_chip(RAVEN2);
		identify_chip(RENOIR);
		break;
	case FAMILY_NV:
		identify_chip(NAVI10); //tested
		identify_chip(NAVI12);
		identify_chip(NAVI14);
		identify_chip(SIENNA_CICHLID);
		identify_chip(NAVY_FLOUNDER);
		identify_chip(DIMGREY_CAVEFISH);
		identify_chip(BEIGE_GOBY);
		break;
	case FAMILY_VGH:
		identify_chip(VANGOGH);
		break;
	case FAMILY_YC:
		identify_chip(YELLOW_CARP);
		break;
	case FAMILY_GFX1036:
		identify_chip(GFX1036);
		break;
	case FAMILY_GFX1037:
		identify_chip(GFX1037);
		break;
	case FAMILY_GFX1100:
		identify_chip(GFX1100);
		identify_chip(GFX1101);
		identify_chip(GFX1102);
		break;
	case FAMILY_GFX1103:
		identify_chip(GFX1103_R1);
		identify_chip(GFX1103_R2);
		break;
	case FAMILY_GFX1150:
		identify_chip(GFX1150);
		break;
	}
	if (!info->name) {
		igt_info("amdgpu: unknown (family_id, chip_external_rev): (%u, %u)\n",
			 amdinfo->family_id, amdinfo->chip_external_rev);
		return -1;
	} else {
		igt_info("amdgpu: %s (family_id, chip_external_rev): (%u, %u)\n",
				info->name, amdinfo->family_id, amdinfo->chip_external_rev);
	}

	if (info->family >= CHIP_GFX1100) {
		info->chip_class = GFX11;
	} else if (info->family >= CHIP_SIENNA_CICHLID) {
		info->chip_class = GFX10_3;
	} else if (info->family >= CHIP_NAVI10) {
		info->chip_class = GFX10;
	} else if (info->family >= CHIP_VEGA10) {
		info->chip_class = GFX9;
	} else if (info->family >= CHIP_TONGA) {
		info->chip_class = GFX8;
	} else if (info->family >= CHIP_BONAIRE) {
		info->chip_class = GFX7;
	} else if (info->family >= CHIP_TAHITI) {
		info->chip_class = GFX6;
	} else {
		igt_info("amdgpu: Unknown family.\n");
		return -1;
	}
	igt_assert_eq(chip_class_arr[info->chip_class].class, info->chip_class);
	igt_info("amdgpu: chip_class %s\n", chip_class_arr[info->chip_class].name);

	switch (info->chip_class) {
	case GFX6:
		break;
	case GFX7: /* tested */
	case GFX8: /* tested */
	case GFX9: /* tested */
	case GFX10:/* tested */
	case GFX10_3: /* tested */
	case GFX11: /* tested */
		amdgpu_device_ip_block_add(&gfx_v8_x_ip_block);
		amdgpu_device_ip_block_add(&compute_v8_x_ip_block);
		amdgpu_device_ip_block_add(&sdma_v3_x_ip_block);
		/*
		 * The handling of a particular family _id is done into
		 * each function and as a result the field family_id would be overwritten
		 * during initialization which matches to actual family_id.
		 * The initial design assumed that for different GPU families, we may
		 * have different implementations, but it is not necessary.
		 * TO DO: move family id as a parameter into IP functions and
		 * remove it as a field
		 */
		for (int i = 0; i <  amdgpu_ips.num_ip_blocks; i++)
			amdgpu_ips.ip_blocks[i]->funcs->family_id = amdinfo->family_id;

		/* extra precaution if re-factor again */
		igt_assert_eq(gfx_v8_x_ip_block.major, 8);
		igt_assert_eq(compute_v8_x_ip_block.major, 8);
		igt_assert_eq(sdma_v3_x_ip_block.major, 3);

		igt_assert_eq(gfx_v8_x_ip_block.funcs->family_id, amdinfo->family_id);
		igt_assert_eq(sdma_v3_x_ip_block.funcs->family_id, amdinfo->family_id);
		break;
	default:
		igt_info("amdgpu: GFX11 or old.\n");
		return -1;
	}
	info->dev = device;

	return 0;
}

int
amdgpu_open_devices(bool open_render_node, int  max_cards_supported, int drm_amdgpu_fds[])
{
	drmDevicePtr devices[MAX_CARDS_SUPPORTED];
	int i;
	int drm_node;
	int amd_index = 0;
	int drm_count;
	int fd;
	drmVersionPtr version;

	for (i = 0; i < max_cards_supported && i < MAX_CARDS_SUPPORTED; i++)
		drm_amdgpu_fds[i] = -1;

	drm_count = drmGetDevices2(0, devices, MAX_CARDS_SUPPORTED);

	if (drm_count < 0) {
		igt_debug("drmGetDevices2() returned an error %d\n", drm_count);
		return 0;
	}

	for (i = 0; i < drm_count; i++) {
		/* If this is not PCI device, skip*/
		if (devices[i]->bustype != DRM_BUS_PCI)
			continue;

		/* If this is not AMD GPU vender ID, skip*/
		if (devices[i]->deviceinfo.pci->vendor_id != 0x1002)
			continue;

		if (open_render_node)
			drm_node = DRM_NODE_RENDER;
		else
			drm_node = DRM_NODE_PRIMARY;

		fd = -1;
		if (devices[i]->available_nodes & 1 << drm_node)
			fd = open(
				devices[i]->nodes[drm_node],
				O_RDWR | O_CLOEXEC);

		/* This node is not available. */
		if (fd < 0)
			continue;

		version = drmGetVersion(fd);
		if (!version) {
			igt_debug("Warning: Cannot get version for %s\n",
				devices[i]->nodes[drm_node]);
			close(fd);
			continue;
		}

		if (strcmp(version->name, "amdgpu")) {
			/* This is not AMDGPU driver, skip.*/
			drmFreeVersion(version);
			close(fd);
			continue;
		}

		drmFreeVersion(version);

		drm_amdgpu_fds[amd_index] = fd;
		amd_index++;
	}

	drmFreeDevices(devices, drm_count);
	return amd_index;
}

/**
 * is_rings_available:
 * @device handle: handle to driver internal information
 * @mask: number of rings we are interested in checking the availability and readiness
 * @type the type of IP, for example GFX, COMPUTE, etc.
 *
 * Check whether the given ring number is ready to accept jobs
 * hw_ip_info.available_rings represents a bit vector of available rings are
 * ready to work.
 */
static bool
is_rings_available(amdgpu_device_handle device_handle, uint32_t mask,
		enum amd_ip_block_type type)
{
	struct drm_amdgpu_info_hw_ip hw_ip_info = {0};
	int r;

	r = amdgpu_query_hw_ip_info(device_handle, type, 0, &hw_ip_info);
	igt_assert_eq(r, 0);

	return  hw_ip_info.available_rings & mask;
}

/**
 * asic_rings_readness:
 * @device handle: handle to driver internal information
 * @mask: number of rings we are interested in checking the availability and readiness
 * @arr array all possible IP ring readiness for the given mask
 *
 * Enumerate all possible IPs by checking their readiness for the given mask.
 */

void
asic_rings_readness(amdgpu_device_handle device_handle, uint32_t mask,
				bool arr[AMD_IP_MAX])
{
	enum amd_ip_block_type ip;
	int i;

	for (i = 0, ip = AMD_IP_GFX; ip < AMD_IP_MAX; ip++)
		arr[i++] = is_rings_available(device_handle, mask, ip);
}

