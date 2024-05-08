/*
 * Copyright © 2016-2023 Intel Corporation
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
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "assembler/brw_compat.h"	/* [un]likely() */

#include "igt_aux.h"
#include "igt_core.h"
#include "igt_kmod.h"
#include "igt_ktap.h"
#include "igt_sysfs.h"
#include "igt_taints.h"

/**
 * SECTION:igt_kmod
 * @short_description: Wrappers around libkmod for module loading/unloading
 * @title: kmod
 * @include: igt.h
 *
 * This library provides helpers to load/unload module driver.
 *
 * Note on loading/reloading:
 *
 * Loading/unload/reloading the driver requires that resources to /dev/dri to
 * be released (closed). A potential mistake would be to submit commands to the
 * GPU by having a fd returned by @drm_open_driver, which is closed by atexit
 * signal handler so reloading/unloading the driver will fail if performed
 * afterwards. One possible solution to this issue is to use
 * @__drm_open_driver() or use @igt_set_module_param() to set module parameters
 * dynamically.
 */

static void squelch(void *data, int priority,
		    const char *file, int line, const char *fn,
		    const char *format, va_list args)
{
}

static struct kmod_ctx *kmod_ctx(void)
{
	static struct kmod_ctx *ctx;
	const char **config_paths = NULL;
	char *config_paths_str;
	char *dirname;

	if (ctx)
		goto out;

	dirname = getenv("IGT_KMOD_DIRNAME");
	if (dirname)
		igt_debug("kmod dirname = %s\n", dirname);

	config_paths_str = getenv("IGT_KMOD_CONFIG_PATHS");
	if (config_paths_str)
		igt_debug("kmod config paths = %s\n", config_paths_str);

	if (config_paths_str) {
		unsigned count = !!strlen(config_paths_str);
		unsigned i;
		char* p;

		p = config_paths_str;
		while ((p = strchr(p, ':'))) p++, count++;


		config_paths = malloc(sizeof(*config_paths) * (count + 1));
		igt_assert(config_paths != NULL);

		p = config_paths_str;
		for (i = 0; i < count; ++i) {
			igt_assert(p != NULL);
			config_paths[i] = p;

			if ((p = strchr(p, ':')))
				*p++ = '\0';
		}
		config_paths[i] = NULL;
	}

	ctx = kmod_new(dirname, config_paths);
	igt_assert(ctx != NULL);

	free(config_paths);

	kmod_set_log_fn(ctx, squelch, NULL);
out:
	return ctx;
}

/**
 * igt_kmod_is_loaded:
 * @mod_name: The name of the module.
 *
 * Returns: True in case the module has been found or false otherwise.
 *
 * Function to check the existance of module @mod_name in list of loaded kernel
 * modules.
 *
 */
bool
igt_kmod_is_loaded(const char *mod_name)
{
	struct kmod_ctx *ctx = kmod_ctx();
	struct kmod_list *mod, *list;
	bool ret = false;

	if (kmod_module_new_from_loaded(ctx, &list) < 0)
		goto out;

	kmod_list_foreach(mod, list) {
		struct kmod_module *kmod = kmod_module_get_module(mod);
		const char *kmod_name = kmod_module_get_name(kmod);

		if (!strcmp(kmod_name, mod_name)) {
			kmod_module_unref(kmod);
			ret = true;
			break;
		}
		kmod_module_unref(kmod);
	}
	kmod_module_unref_list(list);
out:
	return ret;
}

static bool
igt_kmod_is_loading(struct kmod_module *kmod)
{
	return kmod_module_get_initstate(kmod) == KMOD_MODULE_COMING;
}

static int modprobe(struct kmod_module *kmod, const char *options)
{
	unsigned int flags;

	flags = 0;
	if (options) /* force a fresh load to set the new options */
		flags |= KMOD_PROBE_FAIL_ON_LOADED;

	return kmod_module_probe_insert_module(kmod, flags, options,
					       NULL, NULL, NULL);
}

/**
 * igt_kmod_has_param:
 * @mod_name: The name of the module
 * @param: The name of the parameter
 *
 * Returns: true if the module has the parameter, false otherwise.
 */
bool igt_kmod_has_param(const char *module_name, const char *param)
{
	struct kmod_module *kmod;
	struct kmod_list *d, *pre;
	bool result = false;

	if (kmod_module_new_from_name(kmod_ctx(), module_name, &kmod))
		return false;

	pre = NULL;
	if (!kmod_module_get_info(kmod, &pre))
		goto out;

	kmod_list_foreach(d, pre) {
		const char *key, *val;

		key = kmod_module_info_get_key(d);
		if (strcmp(key, "parmtype"))
			continue;

		val = kmod_module_info_get_value(d);
		if (val && strncmp(val, param, strlen(param)) == 0) {
			result = true;
			break;
		}
	}
	kmod_module_info_free_list(pre);

out:
	kmod_module_unref(kmod);
	return result;
}

/**
 * igt_kmod_load:
 * @mod_name: The name of the module
 * @opts: Parameters for the module. NULL in case no parameters
 * are to be passed, or a '\0' terminated string otherwise.
 *
 * This function loads a kernel module using the name specified in @mod_name.
 *
 * Returns: 0 in case of success or -errno in case the module could not
 * be loaded.
 */
int
igt_kmod_load(const char *mod_name, const char *opts)
{
	struct kmod_ctx *ctx = kmod_ctx();
	struct kmod_module *kmod;
	int err = 0;

	err = kmod_module_new_from_name(ctx, mod_name, &kmod);
	if (err < 0)
		goto out;

	err = modprobe(kmod, opts);
	if (err < 0) {
		switch (err) {
		case -EEXIST:
			igt_debug("Module %s already inserted\n",
				  kmod_module_get_name(kmod));
			break;
		case -ENOENT:
			igt_debug("Unknown symbol in module %s or "
				  "unknown parameter\n",
				  kmod_module_get_name(kmod));
			break;
		default:
			igt_debug("Could not insert %s (%s)\n",
				  kmod_module_get_name(kmod), strerror(-err));
			break;
		}
	}
out:
	kmod_module_unref(kmod);
	return err < 0 ? err : 0;
}

static int igt_kmod_unload_r(struct kmod_module *kmod, unsigned int flags)
{
#define MAX_TRIES	20
#define SLEEP_DURATION	500000
	struct kmod_list *holders, *pos;
	int err = 0, tries;
	const char *mod_name = kmod_module_get_name(kmod);

	if (kmod_module_get_initstate(kmod) == KMOD_MODULE_BUILTIN)
		return 0;

	holders = kmod_module_get_holders(kmod);
	kmod_list_foreach(pos, holders) {
		struct kmod_module *it = kmod_module_get_module(pos);
		err = igt_kmod_unload_r(it, flags);
		kmod_module_unref(it);
		if (err < 0)
			break;
	}
	kmod_module_unref_list(holders);
	if (err < 0)
		return err;

	if (igt_kmod_is_loading(kmod)) {
		igt_debug("%s still initializing\n", mod_name);
		err = igt_wait(!igt_kmod_is_loading(kmod), 10000, 100);
		if (err < 0) {
			igt_debug("%s failed to complete init within the timeout\n",
				  mod_name);
			return err;
		}
	}

	for (tries = 0; tries < MAX_TRIES; tries++) {
		err = kmod_module_remove_module(kmod, flags);

		/* Only loop in the following cases */
		if (err != -EBUSY && err != -EAGAIN)
			break;

		igt_debug("Module %s failed to unload with err: %d on attempt: %i\n",
			  mod_name, err, tries + 1);

		if (tries < MAX_TRIES - 1)
			usleep(SLEEP_DURATION);
	}

	if (err == -ENOENT)
		igt_debug("Module %s could not be found or does not exist. err: %d\n",
			  mod_name, err);
	else if (err == -ENOTSUP)
		igt_debug("Module %s cannot be unloaded. err: %d\n",
			  mod_name, err);
	else if (err)
		igt_debug("Module %s failed to unload with err: %d after ~%.1fms\n",
			  mod_name, err, SLEEP_DURATION*tries/1000.);
	else if (tries)
		igt_debug("Module %s unload took ~%.1fms over %i attempts\n",
			  mod_name, SLEEP_DURATION*tries/1000., tries + 1);
	else
		igt_debug("Module %s unloaded immediately\n", mod_name);

	return err;
}

/**
 * igt_kmod_unload:
 * @mod_name: Module name.
 * @flags: flags are passed directly to libkmod and can be:
 * KMOD_REMOVE_FORCE or KMOD_REMOVE_NOWAIT.
 *
 * Returns: 0 in case of success or -errno otherwise.
 *
 * Removes the module @mod_name.
 *
 */
int
igt_kmod_unload(const char *mod_name, unsigned int flags)
{
	struct kmod_ctx *ctx = kmod_ctx();
	struct kmod_module *kmod;
	int err;

	err = kmod_module_new_from_name(ctx, mod_name, &kmod);
	if (err < 0) {
		igt_debug("Could not use module %s (%s)\n", mod_name,
			  strerror(-err));
		goto out;
	}

	err = igt_kmod_unload_r(kmod, flags);
	if (err < 0) {
		igt_debug("Could not remove module %s (%s)\n", mod_name,
			  strerror(-err));
	}

out:
	kmod_module_unref(kmod);
	return err < 0 ? err : 0;
}

/**
 *
 * igt_kmod_list_loaded: List all modules currently loaded.
 *
 */
void
igt_kmod_list_loaded(void)
{
	struct kmod_ctx *ctx = kmod_ctx();
	struct kmod_list *module, *list;

	if (kmod_module_new_from_loaded(ctx, &list) < 0)
		return;

	igt_info("Module\t\t      Used by\n");

	kmod_list_foreach(module, list) {
		struct kmod_module *kmod = kmod_module_get_module(module);
		struct kmod_list *module_deps, *module_deps_list;

		igt_info("%-24s", kmod_module_get_name(kmod));
		module_deps_list = kmod_module_get_holders(kmod);
		if (module_deps_list) {

			kmod_list_foreach(module_deps, module_deps_list) {
				struct kmod_module *kmod_dep;

				kmod_dep = kmod_module_get_module(module_deps);
				igt_info("%s", kmod_module_get_name(kmod_dep));

				if (kmod_list_next(module_deps_list, module_deps))
					igt_info(",");

				kmod_module_unref(kmod_dep);
			}
		}
		kmod_module_unref_list(module_deps_list);

		igt_info("\n");
		kmod_module_unref(kmod);
	}

	kmod_module_unref_list(list);
}

static void *strdup_realloc(char *origptr, const char *strdata)
{
	size_t nbytes = strlen(strdata) + 1;
	char *newptr = realloc(origptr, nbytes);

	memcpy(newptr, strdata, nbytes);
	return newptr;
}

/**
 * igt_intel_driver_load:
 * @opts: options to pass to Intel driver
 *
 * Loads an Intel driver and its dependencies.
 *
 */
int
igt_intel_driver_load(const char *opts, const char *driver)
{
	int ret;

	if (opts)
		igt_info("Reloading %s with %s\n\n", driver, opts);

	ret = igt_kmod_load(driver, opts);
	if (ret) {
		igt_debug("Could not load %s\n", driver);
		return ret;
	}

	bind_fbcon(true);
	igt_kmod_load("snd_hda_intel", NULL);

	return 0;
}

/**
 * kick_snd_hda_intel:
 *
 * This function unbinds the snd_hda_intel driver so the module can be
 * unloaded.
 *
 */
static void kick_snd_hda_intel(void)
{
	DIR *dir;
	struct dirent *snd_hda;
	int fd; size_t len;

	const char *dpath = "/sys/bus/pci/drivers/snd_hda_intel";
	const char *path = "/sys/bus/pci/drivers/snd_hda_intel/unbind";
	const char *devid = "0000:";

	fd = open(path, O_WRONLY);
	if (fd < 0) {
		return;
	}

	dir = opendir(dpath);
	if (!dir)
		goto out;

	len = strlen(devid);
	while ((snd_hda = readdir(dir))) {
		struct stat st;
		char fpath[PATH_MAX];

		if (*snd_hda->d_name == '.')
			continue;

		snprintf(fpath, sizeof(fpath), "%s/%s", dpath, snd_hda->d_name);
		if (lstat(fpath, &st))
			continue;

		if (!S_ISLNK(st.st_mode))
			continue;

		if (!strncmp(devid, snd_hda->d_name, len)) {
			igt_ignore_warn(write(fd, snd_hda->d_name,
					strlen(snd_hda->d_name)));
		}
	}

	closedir(dir);
out:
	close(fd);
}

static int igt_always_unload_audio_driver(char **who)
{
	int ret;
	const char *sound[] = {
		"snd_hda_intel",
		"snd_hdmi_lpe_audio",
		NULL,
	};

	/*
	 * With old Kernels, the dependencies between audio and DRM drivers
	 * are not shown. So, it may not be mandatory to remove the audio
	 * driver before unload/unbind the DRM one. So, let's print warnings,
	 * but return 0 on errors, as, if the dependency is mandatory, this
	 * will be detected later when trying to unbind/unload the DRM driver.
	 */
	for (const char **m = sound; *m; m++) {
		if (igt_kmod_is_loaded(*m)) {
			if (who)
				*who = strdup_realloc(*who, *m);

			ret = igt_lsof_kill_audio_processes();
			if (ret) {
				igt_warn("Could not stop %d audio process(es)\n", ret);
				igt_kmod_list_loaded();
				igt_lsof("/dev/snd");
				return 0;
			}

			ret = pipewire_pulse_start_reserve();
			if (ret)
				igt_warn("Failed to notify pipewire_pulse\n");
			kick_snd_hda_intel();
			ret = igt_kmod_unload(*m, 0);
			pipewire_pulse_stop_reserve();
			if (ret) {
				igt_warn("Could not unload audio driver %s\n", *m);
				igt_kmod_list_loaded();
				igt_lsof("/dev/snd");
				return 0;
			}
		}
	}
	return 0;
}

struct module_ref {
	char *name;
	unsigned long mem;
	unsigned int ref_count;
	unsigned int num_required;
	unsigned int *required_by;
};

int igt_audio_driver_unload(char **who)
{
	/*
	 * Currently, there's no way to check if the audio driver binds into the
	 * DRM one. So, always remove audio drivers that  might be binding.
	 * This may change in future, once kernel/module gets fixed. So, let's
	 * keep this boilerplace, in order to make easier to add the new code,
	 * once upstream is fixed.
	 */
	return igt_always_unload_audio_driver(who);
}

int __igt_intel_driver_unload(char **who, const char *driver)
{
	int ret;

	const char *aux[] = {
		/* gen5: ips uses symbol_get() so only a soft module dependency */
		"intel_ips",
		/* mei_gsc uses an i915 aux dev and the other mei mods depend on it */
		"mei_pxp",
		"mei_hdcp",
		"mei_gsc",
		NULL,
	};

	/* unbind vt */
	bind_fbcon(false);

	ret = igt_audio_driver_unload(who);
	if (ret)
		return ret;

	for (const char **m = aux; *m; m++) {
		if (!igt_kmod_is_loaded(*m))
			continue;

		ret = igt_kmod_unload(*m, 0);
		if (ret) {
			if (who)
				*who = strdup_realloc(*who, *m);

			return ret;
		}
	}

	if (igt_kmod_is_loaded(driver)) {
		ret = igt_kmod_unload(driver, 0);
		if (ret) {
			if (who)
				*who = strdup_realloc(*who, driver);

			return ret;
		}
	}

	return 0;
}

/**
 * igt_intel_driver_unload:
 *
 * Unloads an Intel driver and its dependencies.
 *
 */
int
igt_intel_driver_unload(const char *driver)
{
	char *who = NULL;
	int ret;

	ret = __igt_intel_driver_unload(&who, driver);
	if (ret) {
		igt_warn("Could not unload %s\n", who);
		igt_kmod_list_loaded();
		igt_lsof("/dev/dri");
		igt_lsof("/dev/snd");
		free(who);
		return ret;
	}
	free(who);

	if (igt_kmod_is_loaded("intel-gtt"))
		igt_kmod_unload("intel-gtt", 0);

	igt_kmod_unload("drm_kms_helper", 0);
	igt_kmod_unload("drm", 0);

	if (igt_kmod_is_loaded("driver")) {
		igt_warn("%s.ko still loaded!\n", driver);
		return -EBUSY;
	}

	return 0;
}

/**
 * igt_amdgpu_driver_load:
 * @opts: options to pass to amdgpu driver
 *
 * Returns: IGT_EXIT_SUCCESS or IGT_EXIT_FAILURE.
 *
 * Loads the amdgpu driver and its dependencies.
 *
 */
int
igt_amdgpu_driver_load(const char *opts)
{
	if (opts)
		igt_info("Reloading amdgpu with %s\n\n", opts);

	if (igt_kmod_load("amdgpu", opts)) {
		igt_warn("Could not load amdgpu\n");
		return IGT_EXIT_FAILURE;
	}

	bind_fbcon(true);

	return IGT_EXIT_SUCCESS;
}

/**
 * igt_amdgpu_driver_unload:
 *
 * Returns: IGT_EXIT_SUCCESS on success, IGT_EXIT_FAILURE on failure
 * and IGT_EXIT_SKIP if amdgpu could not be unloaded.
 *
 * Unloads the amdgpu driver and its dependencies.
 *
 */
int
igt_amdgpu_driver_unload(void)
{
	bind_fbcon(false);

	if (igt_kmod_is_loaded("amdgpu")) {
		if (igt_kmod_unload("amdgpu", 0)) {
			igt_warn("Could not unload amdgpu\n");
			igt_kmod_list_loaded();
			igt_lsof("/dev/dri");
			return IGT_EXIT_SKIP;
		}
	}

	igt_kmod_unload("drm_kms_helper", 0);
	igt_kmod_unload("drm", 0);

	if (igt_kmod_is_loaded("amdgpu")) {
		igt_warn("amdgpu.ko still loaded!\n");
		return IGT_EXIT_FAILURE;
	}

	return IGT_EXIT_SUCCESS;
}

static void kmsg_dump(int fd)
{
	char record[4096 + 1];

	if (fd == -1) {
		igt_warn("Unable to retrieve kernel log (from /dev/kmsg)\n");
		return;
	}

	record[sizeof(record) - 1] = '\0';

	for (;;) {
		const char *start, *end;
		ssize_t r;

		r = read(fd, record, sizeof(record) - 1);
		if (r < 0) {
			if (errno == EINTR)
				continue;

			if (errno == EPIPE) {
				igt_warn("kmsg truncated: too many messages. You may want to increase log_buf_len in kmcdline\n");
				continue;
			}

			if (errno != EAGAIN)
				igt_warn("kmsg truncated: unknown error (%m)\n");

			break;
		}

		start = strchr(record, ';');
		if (start) {
			start++;
			end = strchrnul(start, '\n');
			igt_warn("%.*s\n", (int)(end - start), start);
		}
	}
}

static void tests_add(struct igt_kselftest_list *tl, struct igt_list_head *list)
{
	struct igt_kselftest_list *pos;

	igt_list_for_each_entry(pos, list, link)
		if (pos->number > tl->number)
			break;

	igt_list_add_tail(&tl->link, &pos->link);
}

void igt_kselftest_get_tests(struct kmod_module *kmod,
			     const char *filter,
			     struct igt_list_head *tests)
{
	const char *param_prefix = "igt__";
	const int prefix_len = strlen(param_prefix);
	struct kmod_list *d, *pre;
	struct igt_kselftest_list *tl;

	pre = NULL;
	if (!kmod_module_get_info(kmod, &pre))
		return;

	kmod_list_foreach(d, pre) {
		const char *key, *val;
		char *colon;
		int offset;

		key = kmod_module_info_get_key(d);
		if (strcmp(key, "parmtype"))
			continue;

		val = kmod_module_info_get_value(d);
		if (!val || strncmp(val, param_prefix, prefix_len))
			continue;

		offset = strlen(val) + 1;
		tl = malloc(sizeof(*tl) + offset);
		if (!tl)
			continue;

		memcpy(tl->param, val, offset);
		colon = strchr(tl->param, ':');
		*colon = '\0';

		tl->number = 0;
		tl->name = tl->param + prefix_len;
		if (sscanf(tl->name, "%u__%n",
			   &tl->number, &offset) == 1)
			tl->name += offset;

		if (filter && strncmp(tl->name, filter, strlen(filter))) {
			free(tl);
			continue;
		}

		tests_add(tl, tests);
	}
	kmod_module_info_free_list(pre);
}

struct modprobe_data {
	struct kmod_module *kmod;
	const char *opts;
	int err;
	pthread_t parent;
	pthread_mutex_t lock;
	pthread_t thread;
};

static void *modprobe_task(void *arg)
{
	struct modprobe_data *data = arg;

	data->err = modprobe(data->kmod, data->opts);

	if (igt_debug_on(data->err)) {
		bool once = false;
		int err;

		while (err = pthread_mutex_trylock(&data->lock),
		       err && !igt_debug_on(err != EBUSY)) {
			igt_debug_on(pthread_kill(data->parent, SIGCHLD) &&
				     !once);
			once = true;
		}
	} else {
		/* let main thread use mutex to detect modprobe completion */
		igt_debug_on(pthread_mutex_lock(&data->lock));
	}

	return NULL;
}

static void kunit_sigchld_handler(int signal)
{
}

static int kunit_kmsg_result_get(struct igt_list_head *results,
				 struct modprobe_data *modprobe,
				 int fd, struct igt_ktap_results *ktap)
{
	struct sigaction sigchld = { .sa_handler = kunit_sigchld_handler, },
			 *saved;
	char record[BUF_LEN + 1], *buf;
	unsigned long taints;
	int ret;

	do {
		int err;

		if (igt_debug_on(igt_kernel_tainted(&taints)))
			return -ENOTRECOVERABLE;

		if (modprobe) {
			err = igt_debug_on(sigaction(SIGCHLD, &sigchld, saved));
			if (err == -1)
				return -errno;
			else if (unlikely(err))
				return err;

			err = pthread_mutex_lock(&modprobe->lock);
			switch (err) {
			case EOWNERDEAD:
				/* leave the mutex unrecoverable */
				igt_debug_on(pthread_mutex_unlock(&modprobe->lock));
				__attribute__ ((fallthrough));
			case ENOTRECOVERABLE:
				igt_debug_on(sigaction(SIGCHLD, saved, NULL));
				if (igt_debug_on(modprobe->err))
					return modprobe->err;
				break;
			case 0:
				break;
			default:
				igt_debug("pthread_mutex_lock() error: %d\n", err);
				igt_debug_on(sigaction(SIGCHLD, saved, NULL));
				return -err;
			}
		}

		ret = read(fd, record, BUF_LEN);

		if (modprobe && !err) {	/* pthread_mutex_lock() succeeded */
			igt_debug_on(pthread_mutex_unlock(&modprobe->lock));
			igt_debug_on(sigaction(SIGCHLD, saved, NULL));
		}

		if (igt_debug_on(!ret))
			return -ENODATA;
		if (igt_debug_on(ret == -1))
			return -errno;
		if (unlikely(igt_debug_on(ret < 0)))
			break;

		/* skip kmsg continuation lines */
		if (igt_debug_on(*record == ' '))
			continue;

		/* NULL-terminate the record */
		record[ret] = '\0';

		/* detect start of log message, continue if not found */
		buf = strchrnul(record, ';');
		if (igt_debug_on(*buf == '\0'))
			continue;
		buf++;

		ret = igt_ktap_parse(buf, ktap);
		if (!ret || igt_debug_on(ret != -EINPROGRESS))
			break;
	} while (igt_list_empty(results));

	return ret;
}

static void kunit_result_free(struct igt_ktap_result **r,
			      char **suite_name, char **case_name)
{
	if (!*r)
		return;

	igt_list_del(&(*r)->link);

	if ((*r)->suite_name != *suite_name) {
		free(*suite_name);
		*suite_name = (*r)->suite_name;
	}

	if ((*r)->case_name != *case_name) {
		free(*case_name);
		*case_name = (*r)->case_name;
	}

	free((*r)->msg);
	free(*r);
	*r = NULL;
}

static void kunit_results_free(struct igt_list_head *results,
			       char **suite_name, char **case_name)
{
	struct igt_ktap_result *r, *rn;

	igt_list_for_each_entry_safe(r, rn, results, link)
		kunit_result_free(&r, suite_name, case_name);

	free(*case_name);
	free(*suite_name);
}

static void __igt_kunit_legacy(struct igt_ktest *tst,
			       const char *name,
			       const char *opts)
{
	struct modprobe_data modprobe = { tst->kmod, opts, 0, pthread_self(), };
	char *suite_name = NULL, *case_name = NULL;
	struct igt_ktap_results *ktap;
	struct igt_ktap_result *r;
	pthread_mutexattr_t attr;
	IGT_LIST_HEAD(results);
	unsigned long taints;
	int ret;

	igt_skip_on(lseek(tst->kmsg, 0, SEEK_END) < 0);

	igt_skip_on(pthread_mutexattr_init(&attr));
	igt_skip_on(pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST));
	igt_skip_on(pthread_mutex_init(&modprobe.lock, &attr));

	ktap = igt_ktap_alloc(&results);
	igt_require(ktap);

	if (igt_debug_on(pthread_create(&modprobe.thread, NULL,
					modprobe_task, &modprobe))) {
		igt_ktap_free(ktap);
		igt_skip("Failed to create a modprobe thread\n");
	}

	do {
		ret = kunit_kmsg_result_get(&results, &modprobe,
					    tst->kmsg, ktap);
		if (igt_debug_on(ret && ret != -EINPROGRESS))
			break;

		if (igt_debug_on(igt_list_empty(&results)))
			break;

		r = igt_list_first_entry(&results, r, link);

		igt_dynamic_f("%s%s%s",
			      strcmp(r->suite_name, name) ?  r->suite_name : "",
			      strcmp(r->suite_name, name) ? "-" : "",
			      r->case_name) {

			if (r->code == IGT_EXIT_INVALID) {
				/* parametrized test case, get actual result */
				kunit_result_free(&r, &suite_name, &case_name);

				igt_assert(igt_list_empty(&results));

				ret = kunit_kmsg_result_get(&results, &modprobe,
							    tst->kmsg, ktap);
				if (ret != -EINPROGRESS)
					igt_fail_on(ret);

				igt_fail_on(igt_list_empty(&results));

				r = igt_list_first_entry(&results, r, link);

				igt_fail_on_f(strcmp(r->suite_name, suite_name),
					      "suite_name expected: %s, got: %s\n",
					      suite_name, r->suite_name);
				igt_fail_on_f(strcmp(r->case_name, case_name),
					      "case_name expected: %s, got: %s\n",
					      case_name, r->case_name);
			}

			igt_assert_neq(r->code, IGT_EXIT_INVALID);

			if (r->msg && *r->msg) {
				igt_skip_on_f(r->code == IGT_EXIT_SKIP,
					      "%s\n", r->msg);
				igt_fail_on_f(r->code == IGT_EXIT_FAILURE,
					      "%s\n", r->msg);
				igt_abort_on_f(r->code == IGT_EXIT_ABORT,
					      "%s\n", r->msg);
			} else {
				igt_skip_on(r->code == IGT_EXIT_SKIP);
				igt_fail_on(r->code == IGT_EXIT_FAILURE);
				if (r->code == IGT_EXIT_ABORT)
					igt_fail(r->code);
			}
			igt_assert_eq(r->code, IGT_EXIT_SUCCESS);

			switch (pthread_mutex_lock(&modprobe.lock)) {
			case 0:
				igt_debug_on(pthread_mutex_unlock(&modprobe.lock));
				break;
			case EOWNERDEAD:
				/* leave the mutex unrecoverable */
				igt_debug_on(pthread_mutex_unlock(&modprobe.lock));
				__attribute__ ((fallthrough));
			case ENOTRECOVERABLE:
				igt_assert_eq(modprobe.err, 0);
				break;
			default:
				igt_debug("pthread_mutex_lock() failed\n");
				break;
			}

			igt_assert_eq(igt_kernel_tainted(&taints), 0);
		}

		kunit_result_free(&r, &suite_name, &case_name);

	} while (ret == -EINPROGRESS);

	kunit_results_free(&results, &suite_name, &case_name);

	switch (pthread_mutex_lock(&modprobe.lock)) {
	case 0:
		igt_debug_on(pthread_cancel(modprobe.thread));
		igt_debug_on(pthread_mutex_unlock(&modprobe.lock));
		igt_debug_on(pthread_join(modprobe.thread, NULL));
		break;
	case EOWNERDEAD:
		/* leave the mutex unrecoverable */
		igt_debug_on(pthread_mutex_unlock(&modprobe.lock));
		break;
	case ENOTRECOVERABLE:
		break;
	default:
		igt_debug("pthread_mutex_lock() failed\n");
		igt_debug_on(pthread_join(modprobe.thread, NULL));
		break;
	}

	igt_ktap_free(ktap);

	igt_skip_on(modprobe.err);
	igt_skip_on(igt_kernel_tainted(&taints));
	igt_skip_on_f(ret, "KTAP parser failed\n");
}

static void kunit_get_tests(struct igt_list_head *tests,
			    struct igt_ktest *tst,
			    const char *filter,
			    const char *opts)
{
	char *suite_name = NULL, *case_name = NULL;
	struct igt_ktap_result *r, *rn;
	struct igt_ktap_results *ktap;
	int flags, err;

	igt_skip_on_f(tst->kmsg < 0, "Could not open /dev/kmsg\n");

	igt_skip_on((flags = fcntl(tst->kmsg, F_GETFL, 0), flags < 0));
	igt_skip_on_f(fcntl(tst->kmsg, F_SETFL, flags & ~O_NONBLOCK) == -1,
		      "Could not set /dev/kmsg to blocking mode\n");

	igt_skip_on(lseek(tst->kmsg, 0, SEEK_END) < 0);

	/*
	 * To get a list of test cases provided by a kunit test module, ask the
	 * generic kunit module to respond with SKIP result for each test found.
	 * We could also use action=list kunit parameter to get the listing,
	 * however, parsing a KTAP report -- something that we already can do
	 * perfectly -- seems to be more safe than extracting a test case list
	 * of unknown length from /dev/kmsg.
	 *
	 * TODO: drop the following workaround, which is required by LTS kernel
	 *       v6.1 not capable of listing test cases when built as a module.
	 * If loading the kunit module with required parameters fails then
	 * assume that we are running on a kernel with missing test case listing
	 * capabilities.  Dont's skip but just return with empty list of test
	 * cases, that should tell the caller to use a legacy method of
	 * iterating over KTAP results collected from blind execution of all
	 * Kunit test cases provided by a Kunit test module.
	 */
	if (igt_debug_on(igt_kmod_load("kunit",
				       "filter=module=none filter_action=skip")))
		return;

	igt_skip_on(modprobe(tst->kmod, opts));

	ktap = igt_ktap_alloc(tests);
	igt_require(ktap);

	do
		err = kunit_kmsg_result_get(tests, NULL, tst->kmsg, ktap);
	while (err == -EINPROGRESS);

	igt_ktap_free(ktap);

	if (!err)
		igt_list_for_each_entry_safe(r, rn, tests, link) {
			if (igt_debug_on(r->code != IGT_EXIT_SKIP)) {
				err = r->code ?: -EPROTO;
				break;
			}

			if (!filter)
				continue;

			if (strcmp(r->suite_name, filter))
				kunit_result_free(&r, &case_name, &suite_name);
		}

	if (err) {
		kunit_results_free(tests, &case_name, &suite_name);
	} else {
		free(suite_name);
		free(case_name);
	}

	igt_skip_on(kmod_module_remove_module(tst->kmod, KMOD_REMOVE_FORCE));
	igt_skip_on(igt_kmod_unload("kunit", KMOD_REMOVE_FORCE));

	igt_skip_on_f(err,
		      "KTAP parser failed while getting a list of test cases\n");
}

static void __igt_kunit(struct igt_ktest *tst,
			const char *name,
			const char *opts,
			struct igt_list_head *tests)
{
	struct modprobe_data modprobe = { tst->kmod, opts, 0, pthread_self(), };
	char *suite_name = NULL, *case_name = NULL;
	struct igt_ktap_result *t, *r = NULL;
	struct igt_ktap_results *ktap;
	pthread_mutexattr_t attr;
	IGT_LIST_HEAD(results);
	int ret = -EINPROGRESS;
	unsigned long taints;

	igt_skip_on(lseek(tst->kmsg, 0, SEEK_END) < 0);

	ktap = igt_ktap_alloc(&results);
	igt_require(ktap);

	igt_list_for_each_entry(t, tests, link) {
		igt_dynamic_f("%s%s%s",
			      strcmp(t->suite_name, name) ?  t->suite_name : "",
			      strcmp(t->suite_name, name) ? "-" : "",
			      t->case_name) {

			if (!modprobe.thread) {
				igt_assert_eq(pthread_mutexattr_init(&attr), 0);
				igt_assert_eq(pthread_mutexattr_setrobust(&attr,
							  PTHREAD_MUTEX_ROBUST),
					      0);
				igt_assert_eq(pthread_mutex_init(&modprobe.lock,
								 &attr), 0);

				modprobe.err = pthread_create(&modprobe.thread,
							      NULL,
							      modprobe_task,
							      &modprobe);
				igt_assert_eq(modprobe.err, 0);

				igt_assert(igt_list_empty(&results));
				igt_assert_eq(ret, -EINPROGRESS);
				ret = kunit_kmsg_result_get(&results, &modprobe,
							    tst->kmsg, ktap);
				igt_fail_on(igt_list_empty(&results));

				r = igt_list_first_entry(&results, r, link);
			}

			while (igt_debug_on_f(strcmp(r->suite_name, t->suite_name),
					      "suite_name expected: %s, got: %s\n",
					      t->suite_name, r->suite_name) ||
			       igt_debug_on_f(strcmp(r->case_name, t->case_name),
					      "case_name expected: %s, got: %s\n",
					      t->case_name, r->case_name) ||
			       r->code == IGT_EXIT_INVALID) {

				int code = r->code;

				kunit_result_free(&r, &suite_name, &case_name);
				if (igt_list_empty(&results)) {
					igt_assert_eq(ret, -EINPROGRESS);
					ret = kunit_kmsg_result_get(&results,
								    &modprobe,
								    tst->kmsg,
								    ktap);
					igt_fail_on(igt_list_empty(&results));
				}

				r = igt_list_first_entry(&results, r, link);

				if (code != IGT_EXIT_INVALID)
					continue;

				/* result from parametrized test case */
				igt_fail_on_f(strcmp(r->suite_name, suite_name),
					      "suite_name expected: %s, got: %s\n",
					      suite_name, r->suite_name);
				igt_fail_on_f(strcmp(r->case_name, case_name),
					      "case_name expected: %s, got: %s\n",
					      case_name, r->case_name);
			}

			igt_assert_neq(r->code, IGT_EXIT_INVALID);

			if (r->msg && *r->msg) {
				igt_skip_on_f(r->code == IGT_EXIT_SKIP,
					      "%s\n", r->msg);
				igt_fail_on_f(r->code == IGT_EXIT_FAILURE,
					      "%s\n", r->msg);
				igt_abort_on_f(r->code == IGT_EXIT_ABORT,
					      "%s\n", r->msg);
			} else {
				igt_skip_on(r->code == IGT_EXIT_SKIP);
				igt_fail_on(r->code == IGT_EXIT_FAILURE);
				if (r->code == IGT_EXIT_ABORT)
					igt_fail(r->code);
			}
			igt_assert_eq(r->code, IGT_EXIT_SUCCESS);

			switch (pthread_mutex_lock(&modprobe.lock)) {
			case 0:
				igt_debug_on(pthread_mutex_unlock(&modprobe.lock));
				break;
			case EOWNERDEAD:
				/* leave the mutex unrecoverable */
				igt_debug_on(pthread_mutex_unlock(&modprobe.lock));
				__attribute__ ((fallthrough));
			case ENOTRECOVERABLE:
				igt_assert_eq(modprobe.err, 0);
				break;
			default:
				igt_debug("pthread_mutex_lock() failed\n");
				break;
			}

			igt_assert_eq(igt_kernel_tainted(&taints), 0);
		}

		if (igt_debug_on(ret != -EINPROGRESS))
			break;
	}

	kunit_results_free(&results, &suite_name, &case_name);

	if (modprobe.thread) {
		switch (pthread_mutex_lock(&modprobe.lock)) {
		case 0:
			igt_debug_on(pthread_cancel(modprobe.thread));
			igt_debug_on(pthread_mutex_unlock(&modprobe.lock));
			igt_debug_on(pthread_join(modprobe.thread, NULL));
			break;
		case EOWNERDEAD:
			/* leave the mutex unrecoverable */
			igt_debug_on(pthread_mutex_unlock(&modprobe.lock));
			break;
		case ENOTRECOVERABLE:
			break;
		default:
			igt_debug("pthread_mutex_lock() failed\n");
			igt_debug_on(pthread_join(modprobe.thread, NULL));
			break;
		}
	}

	igt_ktap_free(ktap);

	igt_skip_on(modprobe.err);
	igt_skip_on(igt_kernel_tainted(&taints));
	if (ret != -EINPROGRESS)
		igt_skip_on_f(ret, "KTAP parser failed\n");
}

/**
 * igt_kunit:
 * @module_name: the name of the module
 * @name: the name of subtest, if different from that derived from module name
 * @opts: options to load the module
 *
 * Loads the test module, parses its (k)tap dmesg output, then unloads it
 */
void igt_kunit(const char *module_name, const char *name, const char *opts)
{
	struct igt_ktest tst = { .kmsg = -1, };
	const char *filter = name;
	IGT_LIST_HEAD(tests);

	/*
	 * If the caller (an IGT test) provides no subtest name then we
	 * take the module name, drop the trailing "_test" or "_kunit"
	 * suffix, if any, and use the result as our IGT subtest name.
	 */
	if (!name) {
		name = strdup(module_name);
		if (!igt_debug_on(!name)) {
			char *suffix = strstr(name, "_test");

			if (!suffix)
				suffix = strstr(name, "_kunit");

			if (suffix)
				*suffix = '\0';
		}
	}

	igt_fixture {
		igt_require(name);

		igt_skip_on(igt_ktest_init(&tst, module_name));
		igt_skip_on(igt_ktest_begin(&tst));

		/*
		 * Since we need to load kunit base module with specific
		 * options in order to get a list of test cases, make
		 * sure that the module is not loaded.  However, since
		 * unload may fail if kunit base module is not loaded,
		 * ignore any failures, we'll fail later if still loaded.
		 */
		igt_ignore_warn(igt_kmod_unload("kunit", KMOD_REMOVE_FORCE));

		igt_assert(igt_list_empty(&tests));
	}

	/*
	 * We need to use igt_subtest here, as otherwise it may crash with:
	 *  skipping is allowed only in fixtures, subtests or igt_simple_main
	 * if used on igt_main. This is also needed in order to provide
	 * proper namespace for dynamic subtests, with is required for CI
	 * and for documentation.
	 */
	igt_subtest_with_dynamic(name) {
		kunit_get_tests(&tests, &tst, filter, opts);
		/*
		 * TODO: drop the __igt_kunit() legacy processing path, required
		 *	 by kernels v6.1-v6.5 with DRM Kunit support but not
		 *	 capable of listing test cases when built as a module,
		 *	 as soon as no longer required by major Linux
		 *	 distributions, now usually based on LTS kernel v6.1.
		 */
		if (igt_debug_on(igt_list_empty(&tests)))
			__igt_kunit_legacy(&tst, name, opts);
		else
			__igt_kunit(&tst, name, opts, &tests);
	}

	igt_fixture {
		char *suite_name = NULL, *case_name = NULL;

		kunit_results_free(&tests, &suite_name, &case_name);

		igt_ktest_end(&tst);
		igt_debug_on(igt_kmod_unload("kunit", KMOD_REMOVE_FORCE));
	}

	igt_ktest_fini(&tst);
}

static int open_parameters(const char *module_name)
{
	char path[256];

	snprintf(path, sizeof(path), "/sys/module/%s/parameters", module_name);
	return open(path, O_RDONLY);
}

int igt_ktest_init(struct igt_ktest *tst,
		   const char *module_name)
{
	int err;

	memset(tst, 0, sizeof(*tst));

	tst->module_name = strdup(module_name);
	if (!tst->module_name)
		return 1;

	tst->kmsg = -1;

	err = kmod_module_new_from_name(kmod_ctx(), module_name, &tst->kmod);
	if (err)
		return err;

	return 0;
}

int igt_ktest_begin(struct igt_ktest *tst)
{
	int err;

	if (strcmp(tst->module_name, "i915") == 0)
		igt_i915_driver_unload();

	err = kmod_module_remove_module(tst->kmod, KMOD_REMOVE_FORCE);
	igt_require(err == 0 || err == -ENOENT);

	tst->kmsg = open("/dev/kmsg", O_RDONLY | O_NONBLOCK);

	return 0;
}

int igt_kselftest_execute(struct igt_ktest *tst,
			  struct igt_kselftest_list *tl,
			  const char *options,
			  const char *result)
{
	unsigned long taints;
	char buf[1024];
	int err;

	igt_skip_on(igt_kernel_tainted(&taints));

	lseek(tst->kmsg, 0, SEEK_END);

	snprintf(buf, sizeof(buf), "%s=1 %s", tl->param, options ?: "");

	err = modprobe(tst->kmod, buf);
	if (err == 0 && result) {
		int dir = open_parameters(tst->module_name);
		igt_sysfs_scanf(dir, result, "%d", &err);
		close(dir);
	}
	if (err == -ENOTTY) /* special case */
		err = 0;
	if (err)
		kmsg_dump(tst->kmsg);

	kmod_module_remove_module(tst->kmod, 0);

	errno = 0;
	igt_assert_f(err == 0,
		     "kselftest \"%s %s\" failed: %s [%d]\n",
		     tst->module_name, buf, strerror(-err), -err);

	igt_assert_eq(igt_kernel_tainted(&taints), 0);

	return err;
}

void igt_ktest_end(struct igt_ktest *tst)
{
	kmod_module_remove_module(tst->kmod, KMOD_REMOVE_FORCE);
	close(tst->kmsg);
}

void igt_ktest_fini(struct igt_ktest *tst)
{
	free(tst->module_name);
	kmod_module_unref(tst->kmod);
}

static const char *unfilter(const char *filter, const char *name)
{
	if (!filter)
		return name;

	name += strlen(filter);
	if (!isalpha(*name))
		name++;

	return name;
}

void igt_kselftests(const char *module_name,
		    const char *options,
		    const char *result,
		    const char *filter)
{
	struct igt_ktest tst;
	IGT_LIST_HEAD(tests);
	struct igt_kselftest_list *tl, *tn;

	if (igt_ktest_init(&tst, module_name) != 0)
		return;

	igt_fixture
		igt_require(igt_ktest_begin(&tst) == 0);

	igt_kselftest_get_tests(tst.kmod, filter, &tests);
	igt_subtest_with_dynamic(filter ?: "all-tests") {
		igt_list_for_each_entry_safe(tl, tn, &tests, link) {
			unsigned long taints;

			igt_dynamic_f("%s", unfilter(filter, tl->name))
				igt_kselftest_execute(&tst, tl, options, result);
			free(tl);

			if (igt_kernel_tainted(&taints)) {
				igt_info("Kernel tainted, not executing more selftests.\n");
				break;
			}
		}
	}

	igt_fixture {
		igt_ktest_end(&tst);
		igt_require(!igt_list_empty(&tests));
	}

	igt_ktest_fini(&tst);
}
