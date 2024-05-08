/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#include "kms_dsc_helper.h"

static bool force_dsc_en_orig;
static bool force_dsc_fractional_bpp_en_orig;
static int force_dsc_restore_fd = -1;
static int force_dsc_fractional_bpp_restore_fd = -1;

void force_dsc_enable(int drmfd, igt_output_t *output)
{
	int ret;

	igt_debug("Forcing DSC enable on %s\n", output->name);
	ret = igt_force_dsc_enable(drmfd, output->name);
	igt_assert_f(ret == 0, "forcing dsc enable debugfs_write failed\n");
}

void force_dsc_enable_bpc(int drmfd, igt_output_t *output, int input_bpc)
{
	int ret;

	igt_debug("Forcing input DSC BPC to %d on %s\n",
		  input_bpc, output->name);
	ret = igt_force_dsc_enable_bpc(drmfd, output->name, input_bpc);
	igt_assert_f(ret == 0, "forcing input dsc bpc debugfs_write failed\n");
}

void save_force_dsc_en(int drmfd, igt_output_t *output)
{
	force_dsc_en_orig =
		igt_is_force_dsc_enabled(drmfd, output->name);
	force_dsc_restore_fd =
		igt_get_dsc_debugfs_fd(drmfd, output->name);
	igt_assert(force_dsc_restore_fd >= 0);
}

void restore_force_dsc_en(void)
{
	if (force_dsc_restore_fd < 0)
		return;

	igt_debug("Restoring DSC enable\n");
	igt_assert(write(force_dsc_restore_fd, force_dsc_en_orig ? "1" : "0", 1) == 1);

	close(force_dsc_restore_fd);
	force_dsc_restore_fd = -1;
}

void kms_dsc_exit_handler(int sig)
{
	restore_force_dsc_en();
	restore_force_dsc_fractional_bpp_en();
}

bool is_dsc_supported_by_source(int drmfd)
{
	if (!igt_is_dsc_supported_by_source(drmfd)) {
		igt_debug("DSC not supported by source\n");
		return false;
	}

	return true;
}

bool is_dsc_supported_by_sink(int drmfd, igt_output_t *output)
{
	if (!igt_is_dsc_supported_by_sink(drmfd, output->name)) {
		igt_debug("DSC not supported on connector %s\n",
			  output->name);
		return false;
	}

	if (!output_is_internal_panel(output) &&
	    !igt_is_fec_supported(drmfd, output->name)) {
		igt_debug("DSC cannot be enabled without FEC on %s\n",
			  output->name);
		return false;
	}

	return true;
}

bool check_gen11_dp_constraint(int drmfd, igt_output_t *output, enum pipe pipe)
{
	uint32_t devid = intel_get_drm_devid(drmfd);
	drmModeConnector *connector = output->config.connector;

	if ((connector->connector_type == DRM_MODE_CONNECTOR_DisplayPort) &&
	    (pipe == PIPE_A) && IS_GEN11(devid)) {
		igt_debug("DSC not supported on pipe A on external DP in gen11 platforms\n");
		return false;
	}

	return true;
}

/* Max DSC Input BPC for ICL is 10 and for TGL+ is 12 */
bool check_gen11_bpc_constraint(int drmfd, int input_bpc)
{
	uint32_t devid = intel_get_drm_devid(drmfd);

	if (IS_GEN11(devid) && input_bpc == 12) {
		igt_debug("Input bpc 12 not supported on gen11 platforms\n");
		return false;
	}

	return true;
}

void force_dsc_output_format(int drmfd, igt_output_t *output,
			     enum dsc_output_format output_format)
{
	int ret;

	igt_debug("Forcing DSC %s output format on %s\n",
		  kmstest_dsc_output_format_str(output_format), output->name);
	ret = igt_force_dsc_output_format(drmfd, output->name, output_format);
	igt_assert_f(ret == 0, "forcing dsc output format debugfs_write failed\n");
}

/* YCbCr420 DSC is supported on display version 14+ with DSC1.2a */
static bool is_dsc_output_format_supported_by_source(int disp_ver, enum dsc_output_format output_format)
{
	if (disp_ver < 14 && output_format == DSC_FORMAT_YCBCR420) {
		igt_debug("Output format DSC YCBCR420 not supported on D13 and older platforms\n");
		return false;
	}

	return true;
}

bool is_dsc_output_format_supported(int drmfd, int disp_ver, igt_output_t *output,
				    enum dsc_output_format output_format)
{
	if (!is_dsc_output_format_supported_by_source(disp_ver, output_format))
		return false;

	if (!igt_is_dsc_output_format_supported_by_sink(drmfd, output->name, output_format)) {
		igt_debug("DSC %s output format not supported on connector %s\n",
			  kmstest_dsc_output_format_str(output_format), output->name);
		return false;
	}

	return true;
}

void force_dsc_fractional_bpp_enable(int drmfd, igt_output_t *output)
{
	int ret;

	igt_debug("Forcing DSC Fractional BPP on %s\n", output->name);
	ret = igt_force_dsc_fractional_bpp_enable(drmfd, output->name);
	igt_assert_f(ret == 0, "forcing dsc fractional bpp debugfs_write failed\n");
}

void save_force_dsc_fractional_bpp_en(int drmfd, igt_output_t *output)
{
	force_dsc_fractional_bpp_en_orig =
		igt_is_force_dsc_fractional_bpp_enabled(drmfd, output->name);
	force_dsc_fractional_bpp_restore_fd =
		igt_get_dsc_fractional_bpp_debugfs_fd(drmfd, output->name);
	igt_assert(force_dsc_fractional_bpp_restore_fd >= 0);
}

void restore_force_dsc_fractional_bpp_en(void)
{
	if (force_dsc_fractional_bpp_restore_fd < 0)
		return;

	igt_debug("Restoring DSC Fractional BPP enable\n");
	igt_assert(write(force_dsc_fractional_bpp_restore_fd, force_dsc_fractional_bpp_en_orig ? "1" : "0", 1) == 1);

	close(force_dsc_fractional_bpp_restore_fd);
	force_dsc_fractional_bpp_restore_fd = -1;
}

/* DSC fractional bpp is supported on display version 14+ with DSC1.2a */
static bool is_dsc_fractional_bpp_supported_by_source(int disp_ver)
{
	if (disp_ver < 14) {
		igt_debug("DSC fractional bpp not supported on D13 and older platforms\n");
		return false;
	}

	return true;
}

bool is_dsc_fractional_bpp_supported(int disp_ver, int drmfd, igt_output_t *output)
{
	if (!is_dsc_fractional_bpp_supported_by_source(disp_ver))
		return false;

	if (!igt_is_dsc_fractional_bpp_supported_by_sink(drmfd, output->name)) {
		igt_debug("DSC fractional bpp not supported on connector %s\n", output->name);
		return false;
	}

	return true;
}
