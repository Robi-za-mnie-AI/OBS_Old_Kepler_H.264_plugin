#include "obs-nvenc.h"
#include "nvenc-helpers.h"

#include <util/platform.h>
#include <util/threading.h>
#include <util/dstr.h>
#ifndef LEGACY_NVENC_API
#include <util/config-file.h>
#include <util/pipe.h>
#endif

#include <stdarg.h>
#include <stdio.h>

static void *nvenc_lib = NULL;
static pthread_mutex_t init_mutex;
NV_ENCODE_API_FUNCTION_LIST nv = {NV_ENCODE_API_FUNCTION_LIST_VER};
NV_CREATE_INSTANCE_FUNC nv_create_instance = NULL;

static struct encoder_caps encoder_capabilities[2];
static bool codec_supported[2];
static int num_devices;
static int driver_version_major;
static int driver_version_minor;
static bool nvenc_initialized;
static bool nvenc_init_success;

#define error(format, ...) blog(LOG_ERROR, "[obs-nvenc] " format, ##__VA_ARGS__)

typedef NVENCSTATUS(NVENCAPI *NV_MAX_VER_FUNC)(uint32_t *);

static void *load_nv_func(const char *func)
{
	void *func_ptr = os_dlsym(nvenc_lib, func);
	if (!func_ptr)
		error("Could not load function: %s", func);
	return func_ptr;
}

static bool load_nvenc_lib(void)
{
	if (nvenc_lib)
		return true;

#ifdef _WIN32
	nvenc_lib = os_dlopen("nvEncodeAPI64.dll");
#else
	nvenc_lib = os_dlopen("libnvidia-encode.so.1");
#endif
	if (!nvenc_lib)
		error("Could not load the NVIDIA NVENC driver library");
	return nvenc_lib != NULL;
}

static uint32_t get_nvenc_ver(void)
{
	static NV_MAX_VER_FUNC nv_max_ver = NULL;
	static uint32_t ver = 0;
	if (ver)
		return ver;

	if (!load_nvenc_lib())
		return 0;

	if (!nv_max_ver)
		nv_max_ver = (NV_MAX_VER_FUNC)load_nv_func("NvEncodeAPIGetMaxSupportedVersion");
	if (!nv_max_ver)
		return 0;

	if (nv_max_ver(&ver) != NV_ENC_SUCCESS) {
		ver = 0;
		return 0;
	}
	return ver;
}

const char *nv_error_name(NVENCSTATUS err)
{
	switch (err) {
	case NV_ENC_SUCCESS: return "NV_ENC_SUCCESS";
	case NV_ENC_ERR_NO_ENCODE_DEVICE: return "NV_ENC_ERR_NO_ENCODE_DEVICE";
	case NV_ENC_ERR_UNSUPPORTED_DEVICE: return "NV_ENC_ERR_UNSUPPORTED_DEVICE";
	case NV_ENC_ERR_INVALID_ENCODERDEVICE: return "NV_ENC_ERR_INVALID_ENCODERDEVICE";
	case NV_ENC_ERR_INVALID_DEVICE: return "NV_ENC_ERR_INVALID_DEVICE";
	case NV_ENC_ERR_DEVICE_NOT_EXIST: return "NV_ENC_ERR_DEVICE_NOT_EXIST";
	case NV_ENC_ERR_INVALID_PTR: return "NV_ENC_ERR_INVALID_PTR";
	case NV_ENC_ERR_INVALID_EVENT: return "NV_ENC_ERR_INVALID_EVENT";
	case NV_ENC_ERR_INVALID_PARAM: return "NV_ENC_ERR_INVALID_PARAM";
	case NV_ENC_ERR_INVALID_CALL: return "NV_ENC_ERR_INVALID_CALL";
	case NV_ENC_ERR_OUT_OF_MEMORY: return "NV_ENC_ERR_OUT_OF_MEMORY";
	case NV_ENC_ERR_ENCODER_NOT_INITIALIZED: return "NV_ENC_ERR_ENCODER_NOT_INITIALIZED";
	case NV_ENC_ERR_UNSUPPORTED_PARAM: return "NV_ENC_ERR_UNSUPPORTED_PARAM";
	case NV_ENC_ERR_LOCK_BUSY: return "NV_ENC_ERR_LOCK_BUSY";
	case NV_ENC_ERR_NOT_ENOUGH_BUFFER: return "NV_ENC_ERR_NOT_ENOUGH_BUFFER";
	case NV_ENC_ERR_INVALID_VERSION: return "NV_ENC_ERR_INVALID_VERSION";
	case NV_ENC_ERR_MAP_FAILED: return "NV_ENC_ERR_MAP_FAILED";
	case NV_ENC_ERR_NEED_MORE_INPUT: return "NV_ENC_ERR_NEED_MORE_INPUT";
	case NV_ENC_ERR_ENCODER_BUSY: return "NV_ENC_ERR_ENCODER_BUSY";
	case NV_ENC_ERR_EVENT_NOT_REGISTERD: return "NV_ENC_ERR_EVENT_NOT_REGISTERD";
	case NV_ENC_ERR_GENERIC: return "NV_ENC_ERR_GENERIC";
	case NV_ENC_ERR_INCOMPATIBLE_CLIENT_KEY: return "NV_ENC_ERR_INCOMPATIBLE_CLIENT_KEY";
	case NV_ENC_ERR_UNIMPLEMENTED: return "NV_ENC_ERR_UNIMPLEMENTED";
	case NV_ENC_ERR_RESOURCE_REGISTER_FAILED: return "NV_ENC_ERR_RESOURCE_REGISTER_FAILED";
	case NV_ENC_ERR_RESOURCE_NOT_REGISTERED: return "NV_ENC_ERR_RESOURCE_NOT_REGISTERED";
	case NV_ENC_ERR_RESOURCE_NOT_MAPPED: return "NV_ENC_ERR_RESOURCE_NOT_MAPPED";
	default: return "Unknown NVENC error";
	}
}

bool nv_fail2(obs_encoder_t *encoder, void *session, const char *format, ...)
{
	UNUSED_PARAMETER(session);

	struct dstr message = {0};
	struct dstr error_message = {0};
	va_list args;
	va_start(args, format);
	dstr_vprintf(&message, format, args);
	va_end(args);

	dstr_printf(&error_message, "NVENC Error: %s", message.array ? message.array : "");
	obs_encoder_set_last_error(encoder, error_message.array);
	error("%s", error_message.array ? error_message.array : "NVENC Error");

	dstr_free(&error_message);
	dstr_free(&message);
	return true;
}

bool nv_failed2(obs_encoder_t *encoder, void *session, NVENCSTATUS err, const char *func, const char *call)
{
	if (err == NV_ENC_SUCCESS)
		return false;

	const char *nvenc_error = NULL;
#ifndef LEGACY_NVENC_API
	if (session && nv.nvEncGetLastErrorString)
		nvenc_error = nv.nvEncGetLastErrorString(session);
#else
	UNUSED_PARAMETER(session);
#endif

	if (err == NV_ENC_ERR_OUT_OF_MEMORY || err == NV_ENC_ERR_INCOMPATIBLE_CLIENT_KEY) {
		obs_encoder_set_last_error(encoder, obs_module_text("TooManySessions"));
	} else if (err == NV_ENC_ERR_NO_ENCODE_DEVICE || err == NV_ENC_ERR_UNSUPPORTED_DEVICE) {
		obs_encoder_set_last_error(encoder, obs_module_text("UnsupportedDevice"));
	} else if (err == NV_ENC_ERR_INVALID_VERSION) {
		obs_encoder_set_last_error(encoder, obs_module_text("OutdatedDriver"));
	} else {
		struct dstr error_message = {0};
		if (nvenc_error && *nvenc_error)
			dstr_printf(&error_message, "NVENC Error: %s (%s)", nvenc_error, nv_error_name(err));
		else
			dstr_printf(&error_message, "NVENC Error: %s: %s failed: %d (%s)", func, call, (int)err,
				    nv_error_name(err));
		obs_encoder_set_last_error(encoder, error_message.array);
		dstr_free(&error_message);
	}

	if (nvenc_error && *nvenc_error)
		error("%s: %s failed: %d (%s): %s", func, call, (int)err, nv_error_name(err), nvenc_error);
	else
		error("%s: %s failed: %d (%s)", func, call, (int)err, nv_error_name(err));
	return true;
}

#define NV_FAILED(e, x) nv_failed2(e, NULL, x, __FUNCTION__, #x)

static bool init_nvenc_internal(obs_encoder_t *encoder)
{
	if (nvenc_initialized)
		return nvenc_init_success;
	nvenc_initialized = true;

	uint32_t ver = get_nvenc_ver();
	if (ver == 0) {
		obs_encoder_set_last_error(encoder,
			"Missing NvEncodeAPIGetMaxSupportedVersion; check that the NVIDIA driver is installed.");
		return false;
	}

	if (ver < NVCODEC_CONFIGURED_VERSION) {
		obs_encoder_set_last_error(encoder, obs_module_text("OutdatedDriver"));
		error("Driver NVENC API version 0x%x is older than required API version 0x%x",
		      ver, NVCODEC_CONFIGURED_VERSION);
		return false;
	}

	nv_create_instance = (NV_CREATE_INSTANCE_FUNC)load_nv_func("NvEncodeAPICreateInstance");
	if (!nv_create_instance) {
		obs_encoder_set_last_error(encoder,
			"Missing NvEncodeAPICreateInstance; check that the NVIDIA driver is installed.");
		return false;
	}

	if (NV_FAILED(encoder, nv_create_instance(&nv)))
		return false;

	nvenc_init_success = true;
	return true;
}

bool init_nvenc(obs_encoder_t *encoder)
{
	pthread_mutex_lock(&init_mutex);
	bool success = init_nvenc_internal(encoder);
	pthread_mutex_unlock(&init_mutex);
	return success;
}

struct encoder_caps *get_encoder_caps(enum codec_type codec)
{
	if (codec < CODEC_H264 || codec > CODEC_HEVC)
		return &encoder_capabilities[CODEC_H264];
	return &encoder_capabilities[codec];
}

int num_encoder_devices(void)
{
	return num_devices;
}

bool is_codec_supported(enum codec_type codec)
{
	if (codec < CODEC_H264 || codec > CODEC_HEVC)
		return false;
	return codec_supported[codec];
}

bool has_broken_split_encoding(void)
{
	/* CBR padding and tearing artifacts with split encoding are fixed in newer drivers. */
	return driver_version_major < 555;
}

#ifndef LEGACY_NVENC_API
static void read_codec_caps(config_t *config, enum codec_type codec, const char *section)
{
	struct encoder_caps *caps = &encoder_capabilities[codec];
	codec_supported[codec] = config_get_bool(config, section, "codec_supported");
	if (!codec_supported[codec])
		return;

	caps->bframes = (int)config_get_int(config, section, "bframes");
	caps->bref_modes = (int)config_get_int(config, section, "bref");
	caps->engines = (int)config_get_int(config, section, "engines");
	caps->max_width = (int)config_get_int(config, section, "max_width");
	caps->max_height = (int)config_get_int(config, section, "max_height");
	caps->temporal_filter = (int)config_get_int(config, section, "temporal_filter");
	caps->lookahead_level = (int)config_get_int(config, section, "lookahead_level");
	caps->dyn_bitrate = config_get_bool(config, section, "dynamic_bitrate");
	caps->lookahead = config_get_bool(config, section, "lookahead");
	caps->lossless = config_get_bool(config, section, "lossless");
	caps->temporal_aq = config_get_bool(config, section, "temporal_aq");
	caps->ten_bit = config_get_bool(config, section, "10bit");
	caps->four_four_four = config_get_bool(config, section, "yuv_444");
}

static bool nvenc_check(void)
{
#ifdef _WIN32
	char *test_exe = os_get_executable_path_ptr("obs-nvenc-test.exe");
#else
	char *test_exe = os_get_executable_path_ptr("obs-nvenc-test");
#endif
	os_process_args_t *args = os_process_args_create(test_exe);
	struct dstr caps_str = {0};
	config_t *config = NULL;
	bool success = false;

	os_process_pipe_t *pp = os_process_pipe_create2(args, "r");
	if (!pp) {
		blog(LOG_WARNING, "[NVENC] Failed to launch the NVENC test process");
		goto fail;
	}

	for (;;) {
		char data[2048];
		size_t len = os_process_pipe_read(pp, (uint8_t *)data, sizeof(data));
		if (!len)
			break;
		dstr_ncat(&caps_str, data, len);
	}
	os_process_pipe_destroy(pp);

	if (dstr_is_empty(&caps_str)) {
		blog(LOG_WARNING, "[NVENC] NVENC test subprocess returned no data");
		goto fail;
	}
	if (config_open_string(&config, caps_str.array) != 0) {
		blog(LOG_WARNING, "[NVENC] Failed to parse NVENC test output");
		goto fail;
	}

	success = config_get_bool(config, "general", "nvenc_supported");
	if (!success) {
		const char *reason = config_get_string(config, "general", "reason");
		blog(LOG_WARNING, "[NVENC] Test process failed: %s", reason ? reason : "unknown");
		goto fail;
	}

	num_devices = (int)config_get_int(config, "general", "nvenc_devices");
	read_codec_caps(config, CODEC_H264, "h264");
	read_codec_caps(config, CODEC_HEVC, "hevc");

	const char *driver_ver = config_get_string(config, "general", "driver_ver");
	if (driver_ver)
		sscanf(driver_ver, "%d.%d", &driver_version_major, &driver_version_minor);

fail:
	if (config)
		config_close(config);
	bfree(test_exe);
	dstr_free(&caps_str);
	os_process_args_destroy(args);
	return success;
}
#endif

bool nvenc_supported(void)
{
	if (!load_nvenc_lib())
		return false;

#ifdef LEGACY_NVENC_API
	uint32_t ver = get_nvenc_ver();
	if (!ver) {
		error("Could not query the maximum supported NVENC API version");
		return false;
	}
	if (ver < NVCODEC_CONFIGURED_VERSION) {
		error("Driver NVENC API version 0x%x is older than required API version 0x%x",
		      ver, NVCODEC_CONFIGURED_VERSION);
		return false;
	}

	/*
	 * The legacy H.264-only build intentionally does not launch the modern
	 * capability tester. The encoder session itself verifies device support.
	 */
	codec_supported[CODEC_H264] = true;
	codec_supported[CODEC_HEVC] = false;
	num_devices = 1;
	encoder_capabilities[CODEC_H264].bframes = 2;
	encoder_capabilities[CODEC_H264].bref_modes = 0;
	encoder_capabilities[CODEC_H264].engines = 1;
	encoder_capabilities[CODEC_H264].max_width = 4096;
	encoder_capabilities[CODEC_H264].max_height = 4096;
	encoder_capabilities[CODEC_H264].temporal_filter = 0;
	encoder_capabilities[CODEC_H264].lookahead_level = 0;
	encoder_capabilities[CODEC_H264].dyn_bitrate = false;
	encoder_capabilities[CODEC_H264].lookahead = false;
	encoder_capabilities[CODEC_H264].lossless = false;
	encoder_capabilities[CODEC_H264].temporal_aq = false;
	encoder_capabilities[CODEC_H264].ten_bit = false;
	encoder_capabilities[CODEC_H264].four_four_four = false;
	return true;
#else
	return nvenc_check();
#endif
}

void obs_nvenc_load(void)
{
	pthread_mutex_init(&init_mutex, NULL);
	register_encoders();
	register_compat_encoders();
}

void obs_nvenc_unload(void)
{
	pthread_mutex_destroy(&init_mutex);
}
