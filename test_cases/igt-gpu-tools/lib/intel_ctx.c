// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include <stddef.h>

#include "i915/gem_engine_topology.h"
#include "igt_syncobj.h"
#include "intel_allocator.h"
#include "intel_ctx.h"
#include "ioctl_wrappers.h"
#include "xe/xe_ioctl.h"

/**
 * SECTION:intel_ctx
 * @short_description: Wrapper structs for dealing with contexts
 * @title: Intel Context Wrapper
 *
 * This helper library contains a couple of wrapper structs for easier
 * dealing with GEM contexts.  This includes a context configuration struct
 * which represents important context construction parameters and a context
 * struct which contains the context ID and its configuration.  This makes
 * it easier to pass around a context without losing the context create
 * information.
 */

static void
add_user_ext(uint64_t *root_ext_u64, struct i915_user_extension *ext)
{
	ext->next_extension = *root_ext_u64;
	*root_ext_u64 = to_user_pointer(ext);
}

static size_t sizeof_param_engines(int count)
{
	return offsetof(struct i915_context_param_engines, engines[count]);
}

#define SIZEOF_QUERY		offsetof(struct drm_i915_query_engine_info, \
					 engines[GEM_MAX_ENGINES])

/**
 * intel_ctx_cfg_all_physical:
 * @fd: open i915 drm file descriptor
 *
 * Returns an intel_ctx_cfg_t containing all physical engines.  On kernels
 * without the engines API, a default context configuration will be
 * returned.
 */
intel_ctx_cfg_t intel_ctx_cfg_all_physical(int fd)
{
	uint8_t buff[SIZEOF_QUERY] = { };
	struct drm_i915_query_engine_info *qei = (void *) buff;
	intel_ctx_cfg_t cfg = {};
	int i;

	if (__gem_query_engines(fd, qei, SIZEOF_QUERY) == 0) {
		cfg.num_engines = qei->num_engines;
		for (i = 0; i < qei->num_engines; i++)
			cfg.engines[i] = qei->engines[i].engine;
	}

	return cfg;
}

/**
 * intel_ctx_cfg_for_gt:
 * @fd: open i915 drm file descriptor
 * @gt: gt id
 *
 * Returns an intel_ctx_cfg_t containing all physical engines belonging to @gt
 */
intel_ctx_cfg_t intel_ctx_cfg_for_gt(int fd, int gt)
{
	struct i915_engine_class_instance *ci;
	intel_ctx_cfg_t cfg = {};
	unsigned int count;

	ci = gem_list_engines(fd, 1u << gt, ~0u, &count);
	igt_assert(ci);
	memcpy(&cfg.engines, ci, count * sizeof(*ci));
	cfg.num_engines = count;
	free(ci);

	return cfg;
}

/**
 * intel_ctx_cfg_for_engine:
 * @class: engine class
 * @inst: engine instance
 *
 * Returns an intel_ctx_cfg_t containing exactly one engine.
 */
intel_ctx_cfg_t intel_ctx_cfg_for_engine(unsigned int class, unsigned int inst)
{
	return (intel_ctx_cfg_t) {
		.num_engines = 1,
		.engines = {
			{ .engine_class = class, .engine_instance = inst },
		},
	};
}

static int
__context_create_cfg(int fd, const intel_ctx_cfg_t *cfg, uint32_t *ctx_id)
{
	uint64_t ext_root = 0;
	I915_DEFINE_CONTEXT_ENGINES_LOAD_BALANCE(balance, GEM_MAX_ENGINES);
	I915_DEFINE_CONTEXT_ENGINES_PARALLEL_SUBMIT(parallel, GEM_MAX_ENGINES);
	I915_DEFINE_CONTEXT_PARAM_ENGINES(engines, GEM_MAX_ENGINES);
	struct drm_i915_gem_context_create_ext_setparam engines_param, vm_param;
	struct drm_i915_gem_context_create_ext_setparam persist_param;
	uint32_t i;

	if (cfg->vm) {
		vm_param = (struct drm_i915_gem_context_create_ext_setparam) {
			.base = {
				.name = I915_CONTEXT_CREATE_EXT_SETPARAM,
			},
			.param = {
				.param = I915_CONTEXT_PARAM_VM,
				.value = cfg->vm,
			},
		};
		add_user_ext(&ext_root, &vm_param.base);
	}

	if (cfg->nopersist) {
		persist_param = (struct drm_i915_gem_context_create_ext_setparam) {
			.base = {
				.name = I915_CONTEXT_CREATE_EXT_SETPARAM,
			},
			.param = {
				.param = I915_CONTEXT_PARAM_PERSISTENCE,
			},
		};
		add_user_ext(&ext_root, &persist_param.base);
	}

	if (cfg->num_engines) {
		unsigned num_logical_engines;
		memset(&engines, 0, sizeof(engines));

		igt_assert(!(cfg->parallel && cfg->load_balance));

		if (cfg->parallel) {
			memset(&parallel, 0, sizeof(parallel));

			num_logical_engines = 1;

			parallel.base.name =
				I915_CONTEXT_ENGINES_EXT_PARALLEL_SUBMIT;

			engines.engines[0].engine_class =
				I915_ENGINE_CLASS_INVALID;
			engines.engines[0].engine_instance =
				I915_ENGINE_CLASS_INVALID_NONE;

			parallel.num_siblings = cfg->num_engines;
			parallel.width = cfg->width;
			for (i = 0; i < cfg->num_engines * cfg->width; i++) {
				igt_assert_eq(cfg->engines[0].engine_class,
					      cfg->engines[i].engine_class);
				parallel.engines[i] = cfg->engines[i];
			}

			engines.extensions = to_user_pointer(&parallel);
		} else if (cfg->load_balance) {
			memset(&balance, 0, sizeof(balance));

			/* In this case, the first engine is the virtual
			 * balanced engine and the subsequent engines are
			 * the actual requested engines.
			 */
			igt_assert(cfg->num_engines + 1 <= GEM_MAX_ENGINES);
			num_logical_engines = cfg->num_engines + 1;

			balance.base.name =
				I915_CONTEXT_ENGINES_EXT_LOAD_BALANCE;

			engines.engines[0].engine_class =
				I915_ENGINE_CLASS_INVALID;
			engines.engines[0].engine_instance =
				I915_ENGINE_CLASS_INVALID_NONE;

			balance.num_siblings = cfg->num_engines;
			for (i = 0; i < cfg->num_engines; i++) {
				igt_assert_eq(cfg->engines[0].engine_class,
					      cfg->engines[i].engine_class);
				balance.engines[i] = cfg->engines[i];
				engines.engines[i + 1] = cfg->engines[i];
			}

			engines.extensions = to_user_pointer(&balance);
		} else {
			igt_assert(cfg->num_engines <= GEM_MAX_ENGINES);
			num_logical_engines = cfg->num_engines;
			for (i = 0; i < cfg->num_engines; i++)
				engines.engines[i] = cfg->engines[i];
		}

		engines_param = (struct drm_i915_gem_context_create_ext_setparam) {
			.base = {
				.name = I915_CONTEXT_CREATE_EXT_SETPARAM,
			},
			.param = {
				.param = I915_CONTEXT_PARAM_ENGINES,
				.size = sizeof_param_engines(num_logical_engines),
				.value = to_user_pointer(&engines),
			},
		};
		add_user_ext(&ext_root, &engines_param.base);
	} else {
		igt_assert(!cfg->load_balance);
	}

	return __gem_context_create_ext(fd, cfg->flags, ext_root, ctx_id);
}

/**
 * __intel_ctx_create:
 * @fd: open i915 drm file descriptor
 * @cfg: configuration for the created context
 * @out_ctx: on success, the new intel_ctx_t pointer is written here
 *
 * Like intel_ctx_create but returns an error instead of asserting.
 */
int __intel_ctx_create(int fd, const intel_ctx_cfg_t *cfg,
		       const intel_ctx_t **out_ctx)
{
	uint32_t ctx_id;
	intel_ctx_t *ctx;
	int err;

	if (cfg)
		err = __context_create_cfg(fd, cfg, &ctx_id);
	else
		err = __gem_context_create(fd, &ctx_id);
	if (err)
		return err;

	ctx = calloc(1, sizeof(*ctx));
	igt_assert(ctx);

	ctx->id = ctx_id;
	if (cfg)
		ctx->cfg = *cfg;

	*out_ctx = ctx;
	return 0;
}

/**
 * intel_ctx_create:
 * @fd: open i915 drm file descriptor
 * @cfg: configuration for the created context
 *
 * Creates a new intel_ctx_t with the given config.  If @cfg is NULL, a
 * default context is created.
 */
const intel_ctx_t *intel_ctx_create(int fd, const intel_ctx_cfg_t *cfg)
{
	const intel_ctx_t *ctx;
	int err;

	err = __intel_ctx_create(fd, cfg, &ctx);
	igt_assert_eq(err, 0);

	return ctx;
}

static const intel_ctx_t __intel_ctx_0 = {};

/**
 * intel_ctx_0:
 * @fd: open i915 drm file descriptor
 *
 * Returns an intel_ctx_t representing the default context.
 */
const intel_ctx_t *intel_ctx_0(int fd)
{
	(void)fd;
	return &__intel_ctx_0;
}

/**
 * intel_ctx_create_for_engine:
 * @fd: open i915 drm file descriptor
 * @class: engine class
 * @inst: engine instance
 *
 * Returns an intel_ctx_t containing the specified engine.
 */
const intel_ctx_t *
intel_ctx_create_for_engine(int fd, unsigned int class, unsigned int inst)
{
	intel_ctx_cfg_t cfg = intel_ctx_cfg_for_engine(class, inst);
	return intel_ctx_create(fd, &cfg);
}

/**
 * intel_ctx_create_all_physical:
 * @fd: open i915 drm file descriptor
 *
 * Creates an intel_ctx_t containing all physical engines.  On kernels
 * without the engines API, the created context will be the same as
 * intel_ctx_0() except that it will be a new GEM context.  On kernels or
 * hardware which do not support contexts, it is the same as intel_ctx_0().
 */
const intel_ctx_t *intel_ctx_create_all_physical(int fd)
{
	intel_ctx_cfg_t cfg;

	if (!gem_has_contexts(fd))
		return intel_ctx_0(fd);

	cfg = intel_ctx_cfg_all_physical(fd);
	return intel_ctx_create(fd, &cfg);
}

/**
 * intel_ctx_create_for_gt:
 * @fd: open i915 drm file descriptor
 * @gt: gt id
 *
 * Creates an intel_ctx_t containing all physical engines belonging to @gt
 */
const intel_ctx_t *intel_ctx_create_for_gt(int fd, int gt)
{
	intel_ctx_cfg_t cfg;

	igt_require(gem_has_contexts(fd) || !gt);

	if (!gem_has_contexts(fd))
		return intel_ctx_0(fd);

	cfg = intel_ctx_cfg_for_gt(fd, gt);
	return intel_ctx_create(fd, &cfg);
}

/**
 * intel_ctx_cfg_engine_class:
 * @cfg: an intel_ctx_cfg_t
 * @engine: an engine specifier
 *
 * Returns the class for the given engine.
 */
int intel_ctx_cfg_engine_class(const intel_ctx_cfg_t *cfg, unsigned int engine)
{
	if (cfg->load_balance) {
		if (engine == 0) {
			/* This is our virtual engine */
			return cfg->engines[0].engine_class;
		} else {
			/* This is a physical engine */
			igt_assert(engine - 1 < cfg->num_engines);
			return cfg->engines[engine - 1].engine_class;
		}
	} else if (cfg->num_engines > 0) {
		igt_assert(engine < cfg->num_engines);
		return cfg->engines[engine].engine_class;
	} else {
		return gem_execbuf_flags_to_engine_class(engine);
	}
}

/**
 * intel_ctx_destroy:
 * @fd: open i915 drm file descriptor
 * @ctx: context to destroy, or NULL
 *
 * Destroys an intel_ctx_t.
 */
void intel_ctx_destroy(int fd, const intel_ctx_t *ctx)
{
	if (!ctx || ctx->id == 0)
		return;

	gem_context_destroy(fd, ctx->id);
	free((void *)ctx);
}

/**
 * intel_ctx_engine_class:
 * @ctx: an intel_ctx_t
 * @engine: an engine specifier
 *
 * Returns the class for the given engine.
 */
unsigned int intel_ctx_engine_class(const intel_ctx_t *ctx, unsigned int engine)
{
	return intel_ctx_cfg_engine_class(&ctx->cfg, engine);
}

/**
 * intel_ctx_xe:
 * @fd: open i915 drm file descriptor
 * @vm: vm
 * @exec_queue: exec queue
 *
 * Returns an intel_ctx_t representing the xe context.
 */
intel_ctx_t *intel_ctx_xe(int fd, uint32_t vm, uint32_t exec_queue,
			  uint32_t sync_in, uint32_t sync_bind, uint32_t sync_out)
{
	intel_ctx_t *ctx;

	ctx = calloc(1, sizeof(*ctx));
	igt_assert(ctx);

	ctx->fd = fd;
	ctx->vm = vm;
	ctx->exec_queue = exec_queue;
	ctx->sync_in = sync_in;
	ctx->sync_bind = sync_bind;
	ctx->sync_out = sync_out;

	return ctx;
}

int __intel_ctx_xe_exec(const intel_ctx_t *ctx, uint64_t ahnd, uint64_t bb_offset)
{
	struct drm_xe_sync syncs[2] = {
		{ .type = DRM_XE_SYNC_TYPE_SYNCOBJ, },
		{ .type = DRM_XE_SYNC_TYPE_SYNCOBJ, .flags = DRM_XE_SYNC_FLAG_SIGNAL, },
	};
	struct drm_xe_exec exec = {
		.exec_queue_id = ctx->exec_queue,
		.syncs = (uintptr_t)syncs,
		.num_syncs = 2,
		.address = bb_offset,
		.num_batch_buffer = 1,
	};
	uint32_t sync_in = ctx->sync_in;
	uint32_t sync_bind = ctx->sync_bind ?: syncobj_create(ctx->fd, 0);
	uint32_t sync_out = ctx->sync_out ?: syncobj_create(ctx->fd, 0);
	int ret;

	/* Synchronize allocator state -> vm */
	intel_allocator_bind(ahnd, sync_in, sync_bind);

	/* Pipelined exec */
	syncs[0].handle = sync_bind;
	syncs[1].handle = sync_out;

	ret = __xe_exec(ctx->fd, &exec);
	if (ret)
		goto err;

	if (!ctx->sync_bind || !ctx->sync_out)
		syncobj_wait_err(ctx->fd, &sync_out, 1, INT64_MAX, 0);

err:
	if (!ctx->sync_bind)
		syncobj_destroy(ctx->fd, sync_bind);

	if (!ctx->sync_out)
		syncobj_destroy(ctx->fd, sync_out);

	return ret;
}

void intel_ctx_xe_exec(const intel_ctx_t *ctx, uint64_t ahnd, uint64_t bb_offset)
{
	igt_assert_eq(__intel_ctx_xe_exec(ctx, ahnd, bb_offset), 0);
}

#define RESET_SYNCOBJ(__fd, __sync) do { \
	if (__sync) \
		syncobj_reset((__fd), &(__sync), 1); \
} while (0)

int intel_ctx_xe_sync(intel_ctx_t *ctx, bool reset_syncs)
{
	int ret;

	ret = syncobj_wait_err(ctx->fd, &ctx->sync_out, 1, INT64_MAX, 0);

	if (reset_syncs) {
		RESET_SYNCOBJ(ctx->fd, ctx->sync_in);
		RESET_SYNCOBJ(ctx->fd, ctx->sync_bind);
		RESET_SYNCOBJ(ctx->fd, ctx->sync_out);
	}

	return ret;
}
