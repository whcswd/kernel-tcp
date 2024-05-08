/*
 * Copyright © 2007 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *
 */

#ifndef DRMTEST_H
#define DRMTEST_H

#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/mman.h>
#include <errno.h>

#include <xf86drm.h>

#include "igt_core.h"

/*
 * NOTE: Theser are _only_ for testcases exercising driver specific rendering
 * ioctls and uapi (and a bunch of historical reasons). And KMS testcase should
 * be build on top of DRIVER_ANY. Do _NOT_ add your driver here for enabling KMS
 * tests.
 */
#define DRIVER_INTEL	(1 << 0)
#define DRIVER_VC4	(1 << 1)
#define DRIVER_VGEM	(1 << 2)
#define DRIVER_AMDGPU	(1 << 3)
#define DRIVER_V3D	(1 << 4)
#define DRIVER_PANFROST	(1 << 5)
#define DRIVER_MSM	(1 << 6)
#define DRIVER_XE	(1 << 7)
#define DRIVER_VMWGFX   (1 << 8)

/*
 * Exclude DRVER_VGEM from DRIVER_ANY since if you run on a system
 * with vgem as well as a supported driver, you can end up with a
 * near-100% skip rate if you don't explicitly specify the device,
 * depending on device-load ordering.
 */
#define DRIVER_ANY 	~(DRIVER_VGEM)

/*
 * Compile friendly enum for i915/xe.
 */
enum intel_driver {
	INTEL_DRIVER_I915 = 1,
	INTEL_DRIVER_XE,
};

void __set_forced_driver(const char *name);

/**
 * ARRAY_SIZE:
 * @arr: static array
 *
 * Macro to compute the size of the static array @arr.
 */
#define ARRAY_SIZE(arr) (sizeof(arr)/sizeof(arr[0]))

/**
 * ALIGN:
 * @v: value to be aligned
 * @a: alignment unit in bytes
 *
 * Macro to align a value @v to a specified unit @a.
 */
#define ALIGN(v, a) ALIGN_MASK(v, (typeof(v))(a) - 1)
#define ALIGN_MASK(v, mask) (((v) + (mask)) & ~(mask))

/**
 * ALIGN_DOWN:
 * @v: value to be aligned down
 * @a: alignment unit in bytes
 *
 * Macro to align down a value @v to a specified unit @a.
 */
#define ALIGN_DOWN(x, a)	ALIGN((x) - ((a) - 1), (a))

int __drm_open_device(const char *name, unsigned int chipset);
void drm_load_module(unsigned int chipset);
int drm_open_driver(int chipset);
int drm_open_driver_master(int chipset);
int drm_open_driver_render(int chipset);
int __drm_open_driver_another(int idx, int chipset);
int __drm_open_driver(int chipset);
int __drm_open_driver_render(int chipset);
int drm_close_driver(int fd);

int drm_reopen_driver(int fd);

int drm_prepare_filtered_multigpu(int chipset);
int drm_open_filtered_card(int idx);

void igt_require_amdgpu(int fd);
void igt_require_intel(int fd);
void igt_require_i915(int fd);
void igt_require_nouveau(int fd);
void igt_require_vc4(int fd);
void igt_require_xe(int fd);

bool is_amdgpu_device(int fd);
bool is_i915_device(int fd);
bool is_mtk_device(int fd);
bool is_msm_device(int fd);
bool is_nouveau_device(int fd);
bool is_vc4_device(int fd);
bool is_xe_device(int fd);
bool is_intel_device(int fd);
enum intel_driver get_intel_driver(int fd);

/**
 * do_or_die:
 * @x: command
 *
 * Simple macro to execute x and check that it's return value is 0. Presumes
 * that in any failure case the return value is non-zero and a precise error is
 * logged into errno. Uses igt_assert() internally.
 */
#define do_or_die(x) igt_assert((x) == 0)

/**
 * do_ioctl:
 * @fd: open i915 drm file descriptor
 * @ioc: ioctl op definition from drm headers
 * @ioc_data: data pointer for the ioctl operation
 *
 * This macro wraps drmIoctl() and uses igt_assert to check that it has been
 * successfully executed.
 */
#define do_ioctl(fd, ioc, ioc_data) do { \
	igt_assert_eq(igt_ioctl((fd), (ioc), (ioc_data)), 0); \
	errno = 0; \
} while (0)

/**
 * do_ioctl_err:
 * @fd: open i915 drm file descriptor
 * @ioc: ioctl op definition from drm headers
 * @ioc_data: data pointer for the ioctl operation
 * @err: value to expect in errno
 *
 * This macro wraps drmIoctl() and uses igt_assert to check that it fails,
 * returning a particular value in errno.
 */
#define do_ioctl_err(fd, ioc, ioc_data, err) do { \
	igt_assert_eq(igt_ioctl((fd), (ioc), (ioc_data)), -1); \
	igt_assert_eq(errno, err); \
	errno = 0; \
} while (0)

#endif /* DRMTEST_H */
