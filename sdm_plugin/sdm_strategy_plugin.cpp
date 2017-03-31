/*
 * Copyright (c) 2016-2017 The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>

#include <linux/msm_mdp.h>

#include "debug.h"
#include "sdm_strategy_plugin.h"


#ifdef __cplusplus
extern "C" {
#endif

/*
 * StrategyPluginFormatSupport contains the bit map of supported formats for each hw blocks.
 * For eg: if Cursor supports MDP_RGBA_8888[bit-13] and MDP_RGB_565[bit-0], then cursor pipe array
 * contains { 0x01[0-3], 0x00[4-7], 0x00[8-12], 0x01[13-16], 0x00[17-20], 0x00[21-24], 0x00[24-28] }
 */
const std::bitset<8> StrategyPluginFormatSupport[PLUGIN_SUBBLOCK_MAX][BITS_TO_BYTES(MDP_IMGTYPE_LIMIT1)] = {
  { 0xFF, 0xF5, 0x1C, 0x1E, 0x20, 0xFF, 0x01, 0x00, 0xFE, 0x1F },  /* PLUGIN_VIG_PIPE */
  { 0x33, 0xE0, 0x00, 0x16, 0x00, 0xBF, 0x00, 0x00, 0xFE, 0x07 },  /* PLUGIN_RGB_PIPE */
  { 0x33, 0xE0, 0x00, 0x16, 0x00, 0xBF, 0x00, 0x00, 0xFE, 0x07 },  /* PLUGIN_DMA_PIPE */
  { 0x12, 0x60, 0x0C, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00 },  /* PLUGIN_CURSOR_PIPE */
  { 0xFF, 0xF5, 0x1C, 0x1E, 0x20, 0xFF, 0x01, 0x00, 0xFE, 0x1F },  /* PLUGIN_ROTATOR_INPUT */
  { 0xFF, 0xF5, 0x1C, 0x1E, 0x20, 0xFF, 0x01, 0x00, 0xFE, 0x1F },  /* PLUGIN_ROTATOR_OUTPUT */
  { 0x3F, 0xF4, 0x10, 0x1E, 0x20, 0xFF, 0x01, 0x00, 0xAA, 0x16 },  /* PLUGIN_WB_INTF_OUTPUT */
};

struct StrategyPlugin global_plugin;
struct StrategyPlugin *global_plugin_ptr = &global_plugin;

static bool
IsInitialized(void)
{
	return global_plugin_ptr->is_init;
}

static void
PluginLock(void)
{
	pthread_mutex_lock(&global_plugin_ptr->mutex);
}

static void
PluginUnlock(void)
{
	pthread_mutex_unlock(&global_plugin_ptr->mutex);
}

static int
LogFileSetCloexec(FILE *file)
{
	int fd;
	long flags;

	fd = fileno(file);

	if (fd == -1)
		return -1;

	flags = fcntl(fd, F_GETFD);
	if (flags == -1)
		return -1;

	if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1)
		return -1;

	return 0;
}

static void
GetLogFile(void)
{
	const char *path = getenv("SDM_PLUGIN_LOG_FILE");
	FILE *file = NULL;

	if (path) {
		file = fopen(path, "a");
		if (file) {
			int ret;

			ret = LogFileSetCloexec(file);
			if (ret == -1) {
				fclose(file);
				file = NULL;
			}
		}

		if (file) {
			setvbuf(file, NULL, _IOLBF, 256);
			sdm_plugin_log_file = file;
		}
	}
}

static void
GetLogPriority(void)
{
	const char *string = getenv("SDM_PLUGIN_LOG_PRIORITY");
	uint32_t priority = SDM_PLUGIN_DEBUG_LEVEL_VERBOSE;

	if (string) {
		if (strcmp(string, "error") == 0)
			priority = SDM_PLUGIN_DEBUG_LEVEL_ERROR;
		else if (strcmp(string, "warning") == 0)
			priority = SDM_PLUGIN_DEBUG_LEVEL_WARNING;
		else if (strcmp(string, "info") == 0)
			priority = SDM_PLUGIN_DEBUG_LEVEL_INFO;
		else if (strcmp(string, "verbose") == 0)
			priority = SDM_PLUGIN_DEBUG_LEVEL_VERBOSE;
	}

	sdm_plugin_debug_priority = priority;
}

static void
PluginLogInit(void)
{
	GetLogFile();
	GetLogPriority();
}

static void
PluginLogDeinit(void)
{
	if (sdm_plugin_log_file &&
		sdm_plugin_log_file != stderr) {
		fclose(sdm_plugin_log_file);
	}

	sdm_plugin_log_file = stderr;
	sdm_plugin_debug_priority = SDM_PLUGIN_DEBUG_LEVEL_VERBOSE;
}

static sdm::LayerBufferFormat
MDPFormatToSDMFormat(int mdp_format)
{
	switch (mdp_format) {
	case MDP_ARGB_8888:              return sdm::kFormatARGB8888;
	case MDP_RGBA_8888:              return sdm::kFormatRGBA8888;
	case MDP_BGRA_8888:              return sdm::kFormatBGRA8888;
	case MDP_XRGB_8888:              return sdm::kFormatXRGB8888;
	case MDP_RGBX_8888:              return sdm::kFormatRGBX8888;
	case MDP_BGRX_8888:              return sdm::kFormatBGRX8888;
	case MDP_RGBA_5551:              return sdm::kFormatRGBA5551;
	case MDP_RGBA_4444:              return sdm::kFormatRGBA4444;
	case MDP_RGB_888:                return sdm::kFormatRGB888;
	case MDP_BGR_888:                return sdm::kFormatBGR888;
	case MDP_RGB_565:                return sdm::kFormatRGB565;
	case MDP_BGR_565:                return sdm::kFormatBGR565;
	case MDP_RGBA_8888_UBWC:         return sdm::kFormatRGBA8888Ubwc;
	case MDP_RGBX_8888_UBWC:         return sdm::kFormatRGBX8888Ubwc;
	case MDP_RGB_565_UBWC:           return sdm::kFormatBGR565Ubwc;
	case MDP_Y_CB_CR_H2V2:           return sdm::kFormatYCbCr420Planar;
	case MDP_Y_CR_CB_H2V2:           return sdm::kFormatYCrCb420Planar;
	case MDP_Y_CR_CB_GH2V2:          return sdm::kFormatYCrCb420PlanarStride16;
	case MDP_Y_CBCR_H2V2:            return sdm::kFormatYCbCr420SemiPlanar;
	case MDP_Y_CRCB_H2V2:            return sdm::kFormatYCrCb420SemiPlanar;
	case MDP_Y_CBCR_H2V2_VENUS:      return sdm::kFormatYCbCr420SemiPlanarVenus;
	case MDP_Y_CBCR_H1V2:            return sdm::kFormatYCbCr422H1V2SemiPlanar;
	case MDP_Y_CRCB_H1V2:            return sdm::kFormatYCrCb422H1V2SemiPlanar;
	case MDP_Y_CBCR_H2V1:            return sdm::kFormatYCbCr422H2V1SemiPlanar;
	case MDP_Y_CRCB_H2V1:            return sdm::kFormatYCrCb422H2V1SemiPlanar;
	case MDP_Y_CBCR_H2V2_UBWC:       return sdm::kFormatYCbCr420SPVenusUbwc;
	case MDP_Y_CRCB_H2V2_VENUS:      return sdm::kFormatYCrCb420SemiPlanarVenus;
	case MDP_YCBYCR_H2V1:            return sdm::kFormatYCbCr422H2V1Packed;
	case MDP_RGBA_1010102:           return sdm::kFormatRGBA1010102;
	case MDP_ARGB_2101010:           return sdm::kFormatARGB2101010;
	case MDP_RGBX_1010102:           return sdm::kFormatRGBX1010102;
	case MDP_XRGB_2101010:           return sdm::kFormatXRGB2101010;
	case MDP_BGRA_1010102:           return sdm::kFormatBGRA1010102;
	case MDP_ABGR_2101010:           return sdm::kFormatABGR2101010;
	case MDP_BGRX_1010102:           return sdm::kFormatBGRX1010102;
	case MDP_XBGR_2101010:           return sdm::kFormatXBGR2101010;
	case MDP_RGBA_1010102_UBWC:      return sdm::kFormatRGBA1010102Ubwc;
	case MDP_RGBX_1010102_UBWC:      return sdm::kFormatRGBX1010102Ubwc;
	case MDP_Y_CBCR_H2V2_P010:       return sdm::kFormatYCbCr420P010;
	case MDP_Y_CBCR_H2V2_TP10_UBWC:  return sdm::kFormatYCbCr420TP10Ubwc;
	default:                         return sdm::kFormatInvalid;
	}
}

static void
PluginPopulateSupportedFormatMap(const std::bitset<8> *format_supported, uint32_t format_count,
				sdm::HWSubBlockType sub_blk_type, sdm::HWResourceInfo *res_info)
{
	std::vector <sdm::LayerBufferFormat> supported_sdm_formats;
	for (uint32_t mdp_format = 0; mdp_format < format_count; mdp_format++) {
		if (format_supported[mdp_format >> 3][mdp_format & 7]) {
			sdm::LayerBufferFormat sdm_format = MDPFormatToSDMFormat(INT(mdp_format));
			if (sdm_format != sdm::kFormatInvalid) {
				supported_sdm_formats.push_back(sdm_format);
			}
		}
	}

	res_info->supported_formats_map.erase(sub_blk_type);
	res_info->supported_formats_map.insert(make_pair(sub_blk_type, supported_sdm_formats));
}

static void
PluginInitSupportedFormatMap(sdm::HWResourceInfo *res_info)
{
	res_info->supported_formats_map.clear();

	for (int sub_blk_type = INT(PLUGIN_PIPE_TYPE_VIG); sub_blk_type < INT(PLUGIN_SUBBLOCK_MAX); sub_blk_type++) {
		PluginPopulateSupportedFormatMap(StrategyPluginFormatSupport[sub_blk_type], MDP_IMGTYPE_LIMIT1,
				(sdm::HWSubBlockType)sub_blk_type, res_info);
	}
}

static void
PluginInitHwPipe(sdm::HWResourceInfo *res_info, struct PluginPipes *pipes)
{
	for (uint32_t index = 0; index < pipes->count; index++) {
		sdm::HWPipeCaps pipe_caps;
		struct PluginPipeCaps *plugin_pipe_caps = &pipes->pipe_caps[index];

		pipe_caps.type = (sdm::PipeType)plugin_pipe_caps->type;
		if (plugin_pipe_caps->type == PLUGIN_PIPE_TYPE_VIG)
			res_info->num_vig_pipe++;
		else if (plugin_pipe_caps->type == PLUGIN_PIPE_TYPE_RGB)
			res_info->num_rgb_pipe++;
		else if (plugin_pipe_caps->type == PLUGIN_PIPE_TYPE_DMA)
			res_info->num_dma_pipe++;
		else if (plugin_pipe_caps->type == PLUGIN_PIPE_TYPE_CURSOR)
			res_info->num_cursor_pipe++;

		pipe_caps.id = plugin_pipe_caps->id;
		pipe_caps.max_rects = plugin_pipe_caps->max_rects;

		res_info->hw_pipes.push_back(pipe_caps);
	}
}

static int
GetResource(struct HWResourceConfig *config, sdm::HWResourceInfo &info)
{
	PluginInitSupportedFormatMap(&info);

	info.hw_version = 1;
	info.hw_revision = 0;
	info.num_blending_stages = config->num_blending_stages;
	info.num_control = config->num_control;
	info.num_mixer_to_disp = config->num_mixer_to_disp;
	info.smp_total = 0;
	info.smp_size = 0;
	info.num_smp_per_pipe = 0;
	info.max_scale_up = config->max_scale_up;
	info.max_scale_down = config->max_scale_down;
	info.max_bandwidth_low = config->max_bandwidth_low;
	info.max_bandwidth_high = config->max_bandwidth_high;
	info.max_mixer_width = config->max_mixer_width;
	info.max_pipe_width = config->max_pipe_width;
	info.max_cursor_size = config->max_cursor_size;
	info.max_pipe_bw = config->max_pipe_bw;
	info.max_sde_clk = config->max_sde_clk;
	info.clk_fudge_factor = config->clk_fudge_factor;
	info.macrotile_nv12_factor = config->macrotile_nv12_factor;
	info.macrotile_factor = config->macrotile_factor;
	info.linear_factor = config->linear_factor;
	info.scale_factor = config->scale_factor;
	info.extra_fudge_factor = config->extra_fudge_factor;
	info.amortizable_threshold = config->amortizable_threshold;
	info.system_overhead_lines = config->system_overhead_lines;
	info.has_bwc = false;
	info.has_ubwc = config->has_ubwc;
	info.has_decimation = config->has_decimation;
	info.has_macrotile = false; /* set according to user scenarios */
	info.has_non_scalar_rgb = config->has_non_scalar_rgb;
	info.is_src_split = config->is_src_split;
	info.perf_calc = config->perf_calc;
	info.has_dyn_bw_support = config->has_dyn_bw_support;
	info.separate_rotator = config->separate_rotator;
	info.has_qseed3 = config->has_qseed3;
	info.has_concurrent_writeback = config->has_concurrent_writeback;
	info.has_avr = config->has_avr;

	/* HW Rotator info. Now don't support it */
	info.hw_rot_info.type = config->hw_rot_info.type;
	info.hw_rot_info.num_rotator = config->hw_rot_info.num_rotator;
	info.hw_rot_info.has_downscale = config->hw_rot_info.has_downscale;
	info.hw_rot_info.device_path = "";

	/* HW scalar info*/
	info.hw_dest_scalar_info.count = config->hw_dest_scalar_info.count;
	info.hw_dest_scalar_info.max_input_width = config->hw_dest_scalar_info.max_input_width;
	info.hw_dest_scalar_info.max_output_width = config->hw_dest_scalar_info.max_output_width;
	info.hw_dest_scalar_info.max_scale_up = config->hw_dest_scalar_info.max_scale_up;

	/* HW pipe initialization */
	PluginInitHwPipe(&info, &config->hw_pipes);

	info.dyn_bw_info.cur_mode = config->dyn_bw_info.cur_mode;
	for (int i = 0; i < PLUGIN_BW_MODE_MAX; i++) {
		info.dyn_bw_info.total_bw_limit[i] = config->dyn_bw_info.total_bw_limit[i];
		info.dyn_bw_info.pipe_bw_limit[i] = config->dyn_bw_info.pipe_bw_limit[i];
	}

	return PLUGIN_ERROR_NONE;
}

static int
GetDisplayAttribs(struct PluginDisplayAttributes *src, sdm::HWDisplayAttributes &attribs)
{
	attribs.is_device_split = src->is_device_split;
	attribs.v_front_porch = src->v_front_porch;
	attribs.v_back_porch = src->v_back_porch;
	attribs.v_pulse_width = src->v_pulse_width;
	attribs.h_total = src->h_total;
	attribs.x_pixels = src->config_variable_info.x_pixels;
	attribs.y_pixels = src->config_variable_info.y_pixels;
	attribs.x_dpi = src->config_variable_info.x_dpi;
	attribs.y_dpi = src->config_variable_info.y_dpi;
	attribs.fps = src->config_variable_info.fps;
	attribs.vsync_period_ns = src->config_variable_info.vsync_period_ns;
	attribs.is_yuv = src->config_variable_info.is_yuv;

	return PLUGIN_ERROR_NONE;
}

static int
GetPanelInfo(struct PluginPanelInfo *src, sdm::HWPanelInfo &panel)
{
	panel.port = (sdm::DisplayPort)src->port;
	panel.mode = (sdm::HWDisplayMode)src->mode;
	panel.partial_update = src->partial_update;
	panel.left_align = src->left_align;
	panel.width_align = src->width_align;
	panel.top_align = src->top_align;
	panel.height_align = src->height_align;
	panel.min_roi_width = src->min_roi_width;
	panel.min_roi_height = src->min_roi_height;
	panel.needs_roi_merge = src->needs_roi_merge;
	panel.dynamic_fps = src->dynamic_fps;
	panel.min_fps = src->min_fps;
	panel.max_fps = src->max_fps;
	panel.is_primary_panel = src->is_primary_panel;
	panel.split_info.left_split = src->left_split;
	panel.split_info.right_split = src->right_split;
	size_t length = strlen(src->panel_name);
	if (length >= 256)
		length = 255;
	strncpy(panel.panel_name, src->panel_name, length);
	panel.panel_name[length] = '\0';
	return PLUGIN_ERROR_NONE;
}

static void
GetMixerAttributes(sdm::HWDisplayAttributes *disp_attribs, sdm::HWPanelInfo *panel,
			sdm::HWMixerAttributes &mixer_attributes)
{
	mixer_attributes.width = disp_attribs->x_pixels;
	mixer_attributes.height = disp_attribs->y_pixels;
	mixer_attributes.split_left = disp_attribs->is_device_split ?
		panel->split_info.left_split : mixer_attributes.width;
}

static void
GetFBConfig(sdm::HWDisplayAttributes *disp_attribs, sdm::HWMixerAttributes *mixer_attributes, sdm::DisplayConfigVariableInfo &fb_config)
{
	fb_config = *disp_attribs;
	/* Override x_pixels and y_pixels of frame buffer with mixer width and height */
	fb_config.x_pixels = mixer_attributes->width;
	fb_config.y_pixels = mixer_attributes->height;
}

plugin_handle_t
SDMPluginInit(struct StrategyPluginConfig *config, uint32_t flags)
{
	struct StrategyPlugin *strategy_plugin = &global_plugin;
	PluginBufferSyncHandler *buffer_sync_handler = NULL;
	struct StrategyClientContext *client_ctx = NULL;
	sdm::ResourceImpl *res_impl = NULL;
	int error = PLUGIN_ERROR_NONE;

	PluginLock();

	/* If global plugin is already initialized, just return the pointer of it */
	if (IsInitialized()) {
		PluginLock();
		return static_cast<void *>(strategy_plugin);
	}

	if (!config) {
		PluginUnlock();
		SDM_PLUGIN_LOGE("plugin config is NULL\n");
		return NULL;
	}

	buffer_sync_handler = new PluginBufferSyncHandler();
	if (buffer_sync_handler == NULL) {
		PluginUnlock();
		SDM_PLUGIN_LOGE("out of memory for PluginBufferSyncHandler\n");
		return NULL;
	}

	/* Get HWResource */
	sdm::HWResourceInfo &hw_res_info = strategy_plugin->hw_res_info;
	error = GetResource(&config->hw_res_config, hw_res_info);
	if (error != PLUGIN_ERROR_NONE) {
		SDM_PLUGIN_LOGE("fail to parse hw resource information\n");
		goto error_hw_res_info;
	}

	/* Get ResourceInterface */
	res_impl = new sdm::ResourceImpl(hw_res_info);
	if (res_impl == NULL) {
		SDM_PLUGIN_LOGE("out of memory for ResourceImpl\n");
		goto error_hw_res_info;
	}

	if(res_impl->Init(buffer_sync_handler)) {
		SDM_PLUGIN_LOGE("fail to initialize resource manager \n");
		goto error_res_impl;
	}

	strategy_plugin->compositor_caps = config->compositor_caps;
	strategy_plugin->buf_sync_handler = buffer_sync_handler;
	strategy_plugin->res_impl = res_impl;
	strategy_plugin->hw_res_info = hw_res_info;
	strategy_plugin->displays_mask = 0;

	client_ctx = &strategy_plugin->client_ctx;
	memset(client_ctx, 0, sizeof(*client_ctx));

	strategy_plugin->is_init = true;

	PluginLogInit();

	PluginUnlock();
	return static_cast<plugin_handle_t>(strategy_plugin);

error_res_impl:
	if (res_impl)
		delete res_impl;
error_hw_res_info:
	strategy_plugin->hw_res_info.Reset();
	if (buffer_sync_handler)
		delete buffer_sync_handler;
	PluginUnlock();
	return NULL;
}

static int
FindClientIndex(struct StrategyClient *client)
{
	struct StrategyClientContext *ctx = &global_plugin_ptr->client_ctx;
	int i;

	if (!client) {
		SDM_PLUGIN_LOGE("client can't be NULL\n");
		return -1;
	}

	for (i = 0; i < MAX_STRATEGY_CLIENTS; i++) {
		if (ctx->clients[i] == client)
			break;
	}

	if (i == MAX_STRATEGY_CLIENTS) {
		SDM_PLUGIN_LOGE("no mattched client handle %p\n", client);
		return -1;
	}

	return i;
}

client_handle_t
SDMPluginCreateClient(struct ClientConfigs *configs, uint32_t flags)
{
	struct StrategyClient *client = NULL;
	struct StrategyClientContext *ctx = &global_plugin_ptr->client_ctx;
	int i;

	PluginLock();

	if(!IsInitialized()) {
		SDM_PLUGIN_LOGE("plugin is not initialized\n");
		PluginUnlock();
		return NULL;
	}

	if (ctx->count == MAX_STRATEGY_CLIENTS) {
		SDM_PLUGIN_LOGE("exceed the max supported clients\n");
		goto error_validation;
	}

	client = new StrategyClient();
	if (client == NULL) {
		SDM_PLUGIN_LOGE("fail to create client\n");
		goto error_validation;
	}

	for (i = 0; i < MAX_STRATEGY_CLIENTS; i++) {
		if (ctx->clients[i] == NULL)
			break;
	}

	if (i == MAX_STRATEGY_CLIENTS) {
		SDM_PLUGIN_LOGE("no slot for client. Should not happen\n");
		goto error_no_slot;
	}

	ctx->clients[i] = client;
	ctx->count++;

	PluginUnlock();
	return static_cast<client_handle_t>(client);

error_no_slot:
	if (client)
		delete client;

error_validation:
	PluginUnlock();
	return NULL;
}

static int
CreateStrategyMgr(struct StrategyContext *ctx, sdm::DisplayType type, sdm::HWResourceInfo &hw_res_info,
		sdm::HWDisplayAttributes &attribs, sdm::HWPanelInfo &panel, const sdm::HWMixerAttributes &mixer_attributes,
		const sdm::DisplayConfigVariableInfo &fb_config)
{
	sdm::StrategyImpl *strategy_impl = NULL;
	sdm::PartialUpdateImpl *partial_update_impl = NULL;
	sdm::DisplayError error = sdm::kErrorNone;
	
	/* Strategy initialization */
	strategy_impl = new sdm::StrategyImpl(type, panel.mode, panel.s3d_mode, mixer_attributes, fb_config);
	if (strategy_impl == NULL) {
		SDM_PLUGIN_LOGE("fail to create StrategyImpl\n");
		return PLUGIN_ERROR_MEMORY;
	}

	error = strategy_impl->Init();
	if (error != sdm::kErrorNone) {
		SDM_PLUGIN_LOGE("fail to initialize StrategyImpl\n");
		return PLUGIN_ERROR_SDM;
	}

	/* Partial update intialization */
	sdm::PartialUpdateImpl::Create(type,
					hw_res_info,
					panel,
					mixer_attributes,
					attribs,
					&partial_update_impl);
	if (partial_update_impl == NULL) {
		SDM_PLUGIN_LOGI("No partial update.\n");
	}

	ctx->strategy_impl = strategy_impl;
	ctx->partial_update_impl = partial_update_impl;

	return PLUGIN_ERROR_NONE;
}

static void
DestroyStrategyMgr(struct StrategyContext *strategy_ctx)
{
	if (strategy_ctx->partial_update_impl) {
		sdm::PartialUpdateImpl::Destroy(strategy_ctx->partial_update_impl);
		strategy_ctx->partial_update_impl = NULL;
	}

	if (strategy_ctx->strategy_impl) {
		strategy_ctx->strategy_impl->Deinit();
		delete strategy_ctx->strategy_impl;
		strategy_ctx->strategy_impl = NULL;
	}
}

display_handle_t
SDMPluginCreateDisplay(client_handle_t handle, PluginDisplayParameter *para, uint32_t flags)
{
	struct StrategyClient *client = static_cast<struct StrategyClient *>(handle);
	struct Display *display = NULL;
	struct StrategyContext *strategy_ctx = NULL;
	sdm::Handle disp_res_ctx = NULL;
	int error = PLUGIN_ERROR_NONE;
	int index;
	uint32_t type, disp_mask;
	uint32_t max_blending_stages;

	PluginLock();

	if(!IsInitialized()) {
		SDM_PLUGIN_LOGE("plugin is not initialized\n");
		goto error_validation;
	}

	if (!para) {
		SDM_PLUGIN_LOGE("display parameter is invalid\n");
		goto error_validation;
	}

	index = FindClientIndex(client);
	if (index == -1) {
		goto error_validation;
	}

	type = para->type;
	if (type >= PLUGIN_MAX_DISPLAYS) {
		SDM_PLUGIN_LOGE("display type %d is not supported!\n", type);
		goto error_validation;
	}

	disp_mask = 1 << type;
	/* Check if the slot is already occupied, can't create a display which is already created */
	if (disp_mask & global_plugin_ptr->displays_mask) {
		SDM_PLUGIN_LOGE("display is already created!\n");
		goto error_validation;
	}

	display = new Display();
	if (display == NULL) {
		SDM_PLUGIN_LOGE("out of memory for display\n");
		goto error_display;
	}

	/* Get display attibutes and panel information */
	error = GetDisplayAttribs(&para->attribs, display->attribs);
	if (error != PLUGIN_ERROR_NONE) {
		SDM_PLUGIN_LOGE("fail to get display attributes\n");
		goto error_display;
	}

	error = GetPanelInfo(&para->panel, display->panel);
	if (error != PLUGIN_ERROR_NONE) {
		SDM_PLUGIN_LOGE("fail to get panel information\n");
		goto error_display;
	}

	/* Get mixer attributes */
	GetMixerAttributes(&display->attribs, &display->panel, display->mixer_attributes);

	/* Get fb config */
	GetFBConfig(&display->attribs, &display->mixer_attributes, display->fb_config);

	/* Create strategy manager */
	strategy_ctx = &display->ctx.strategy_ctx;
	error = CreateStrategyMgr(strategy_ctx, (sdm::DisplayType)type,
				global_plugin_ptr->hw_res_info,
				display->attribs,
				display->panel,
				display->mixer_attributes,
				display->fb_config);
	if (error != PLUGIN_ERROR_NONE) {
		goto error_strategy_mgr;
	}

	/* Register display and get display resource context */
	error = global_plugin_ptr->res_impl->RegisterDisplay((sdm::DisplayType)type,
						display->attribs,
						display->panel,
						display->mixer_attributes,
						&disp_res_ctx);
	if (error != sdm::kErrorNone) {
		SDM_PLUGIN_LOGE("fail to register display\n");
		goto error_strategy_mgr;
	}

	max_blending_stages = para->max_blending_stages;
	if (max_blending_stages < 1 ||
		max_blending_stages > global_plugin_ptr->hw_res_info.num_blending_stages)
		max_blending_stages = global_plugin_ptr->hw_res_info.num_blending_stages;
	global_plugin_ptr->res_impl->SetMaxMixerStages(disp_res_ctx, max_blending_stages);

	display->ctx.type = (sdm::DisplayType)type;
	display->ctx.disp_res_ctx = disp_res_ctx;
	display->ctx.idle_fallback = false;
	display->ctx.fallback = false;
	display->ctx.max_layers = para->max_layers;
	display->ctx.partial_update_enable = false;/* different from default value since it's not support now */
	display->layer_stack = sdm::LayerStack();

	client->displays[type] = display;
	global_plugin_ptr->displays_mask |= disp_mask;

	PluginUnlock();
	return static_cast<display_handle_t>(display);

error_strategy_mgr:
	DestroyStrategyMgr(strategy_ctx);
error_display:
	if (display)
		delete display;
error_validation:
	PluginUnlock();

	return NULL;
}

static void
DoDestroyDisplay(struct Display *d)
{
	sdm::Handle disp_res_ctx = d->ctx.disp_res_ctx;
	struct StrategyContext *strategy_ctx = &d->ctx.strategy_ctx;

	if (global_plugin_ptr->res_impl)
		global_plugin_ptr->res_impl->UnregisterDisplay(disp_res_ctx);

	DestroyStrategyMgr(strategy_ctx);

	delete d;
}

static int
FindDisplaySlot(struct StrategyClient *client, struct Display *d)
{
	int i;

	if (!d) {
		SDM_PLUGIN_LOGE("display can't be NULL!\n");
		return -1;
	}

	for (i = 0; i < PLUGIN_MAX_DISPLAYS; i++) {
		if (d == client->displays[i])
			break;
	}

	if (i == PLUGIN_MAX_DISPLAYS) {
		SDM_PLUGIN_LOGE("no matched display %p for client %p\n", d, client);
		return -1;
	}

	return i;
}

int
SDMPluginDestroyDisplay(client_handle_t handle, display_handle_t display)
{
	struct StrategyClient *client = static_cast<struct StrategyClient *>(handle);
	struct Display *d = static_cast<struct Display *>(display);
	int index;
	uint32_t disp_mask;

	PluginLock();

	if(!IsInitialized()) {
		SDM_PLUGIN_LOGE("plugin is not initialized\n");
		PluginUnlock();
		return PLUGIN_ERROR_MEMORY;
	}

	index = FindClientIndex(client);
	if (index == -1) {
		PluginUnlock();
		return PLUGIN_ERROR_PARAMETER;
	}

	index = FindDisplaySlot(client, d);
	if(index == -1) {
		PluginUnlock();
		return PLUGIN_ERROR_PARAMETER;
	}

	disp_mask = 1 << d->ctx.type;
	DoDestroyDisplay(d);
	client->displays[index] = NULL;
	global_plugin_ptr->displays_mask &= ~disp_mask;

	PluginUnlock();
	return PLUGIN_ERROR_NONE;
}

int
SDMPluginDestroyClient(client_handle_t handle)
{
	struct StrategyClient *client = static_cast<struct StrategyClient *>(handle);
	struct StrategyClientContext *ctx = &global_plugin_ptr->client_ctx;
	int i, index;
	uint32_t disp_mask;

	PluginLock();

	if(!IsInitialized()) {
		SDM_PLUGIN_LOGE("plugin is not initialized\n");
		PluginUnlock();
		return PLUGIN_ERROR_MEMORY;
	}

	index = FindClientIndex(client);
	if (index == -1) {
		PluginUnlock();
		return PLUGIN_ERROR_PARAMETER;
	}

	/*
	 * Release client resources. If the cached properties have not been flushed yet,
	 * just drop them and do nothing.
	 */
	for (i = 0; i < PLUGIN_MAX_DISPLAYS; i++) {
		Display *d = client->displays[i];
		if (d) {
			disp_mask = 1 << d->ctx.type;
			DoDestroyDisplay(d);
			client->displays[i] = NULL;
			global_plugin_ptr->displays_mask &= ~disp_mask;
		}
	}

	delete client;
	ctx->clients[index] = NULL;
	ctx->count--;

	PluginUnlock();

	return PLUGIN_ERROR_NONE;
}

static int
AllocLayerStackMemory(struct Display *d, struct PluginLayersConfig *configs)
{
	uint32_t i, total_layers;
	sdm::LayerStack *layer_stack = &d->layer_stack;


	if (configs->count == 0 || configs->layers == NULL) {
		SDM_PLUGIN_LOGE("no layer config?!\n");
		return PLUGIN_ERROR_PARAMETER;
	}

	/* Free old layer stack */
	for (sdm::Layer *layer : layer_stack->layers) {
		layer->visible_regions.clear();
		layer->dirty_regions.clear();
		delete layer->input_buffer;
		delete layer;
	}
	*layer_stack = {};

	/* Assuming FB target is involved in the configs */
	total_layers = configs->count;
	for (i = 0; i < total_layers; i++) {
		sdm::Layer *layer = new sdm::Layer();
		sdm::LayerBuffer *layer_buffer = new sdm::LayerBuffer();
		layer->input_buffer = layer_buffer;
		layer_stack->layers.push_back(layer);
	}

	return PLUGIN_ERROR_NONE;
}

static sdm::LayerBufferFormat
PluginFormatToSDMFormat(uint32_t src_fmt, struct LayerFlags flags)
{
	sdm::LayerBufferFormat format = sdm::kFormatInvalid;

	if (flags.has_ubwc_buf) {
		switch (src_fmt) {
			case PLUGIN_BUFFER_FORMAT_RGBA_8888:
				format = sdm::kFormatRGBA8888Ubwc;
				break;
			case PLUGIN_BUFFER_FORMAT_RGBX_8888:
				format = sdm::kFormatRGBX8888Ubwc;
				break;
			case PLUGIN_BUFFER_FORMAT_BGR_565:
				format = sdm::kFormatBGR565Ubwc;
				break;
			case PLUGIN_BUFFER_FORMAT_YCbCr_420_SP_VENUS:
			case PLUGIN_BUFFER_FORMAT_NV12_ENCODEABLE:
				format = sdm::kFormatYCbCr420SPVenusUbwc;
				break;
			default:
				SDM_PLUGIN_LOGE("Unsupported UBWC format %d\n", src_fmt);
				return sdm::kFormatInvalid;
		}
		return format;
	}

	switch (src_fmt) {
		case PLUGIN_BUFFER_FORMAT_RGBA_8888:
			format = sdm::kFormatRGBA8888;
			break;
		case PLUGIN_BUFFER_FORMAT_RGBA_5551:
			format = sdm::kFormatRGBA5551;
			break;
		case PLUGIN_BUFFER_FORMAT_RGBA_4444:
			format = sdm::kFormatRGBA4444;
			break;
		case PLUGIN_BUFFER_FORMAT_BGRA_8888:
			format = sdm::kFormatBGRA8888;
			break;
		case PLUGIN_BUFFER_FORMAT_RGBX_8888:
			format = sdm::kFormatRGBX8888;
			break;
		case PLUGIN_BUFFER_FORMAT_BGRX_8888:
			format = sdm::kFormatBGRX8888;
			break;
		case PLUGIN_BUFFER_FORMAT_RGB_888:
			format = sdm::kFormatRGB888;
			break;
		case PLUGIN_BUFFER_FORMAT_RGB_565:
			format = sdm::kFormatRGB565;
			break;
		case PLUGIN_BUFFER_FORMAT_BGR_565:
			format = sdm::kFormatBGR565;
			break;
		case PLUGIN_BUFFER_FORMAT_NV12_ENCODEABLE:
		case PLUGIN_BUFFER_FORMAT_YCbCr_420_SP_VENUS:
			format = sdm::kFormatYCbCr420SemiPlanarVenus;
			break;
		case PLUGIN_BUFFER_FORMAT_YV12:
			format = sdm::kFormatYCrCb420PlanarStride16;
			break;
		case PLUGIN_BUFFER_FORMAT_YCrCb_420_SP:
			format = sdm::kFormatYCrCb420SemiPlanar;
			break;
		case PLUGIN_BUFFER_FORMAT_YCbCr_420_SP:
			format = sdm::kFormatYCbCr420SemiPlanar;
			break;
		case PLUGIN_BUFFER_FORMAT_YCbCr_422_SP:
			format = sdm::kFormatYCbCr422H2V1SemiPlanar;
			break;
		case PLUGIN_BUFFER_FORMAT_YCbCr_422_I:
			format = sdm::kFormatYCbCr422H2V1Packed;
			break;
		default:
			SDM_PLUGIN_LOGE("Unsupported format %d\n", src_fmt);
			return sdm::kFormatInvalid;
	}

	return format;
}

static void
SetRect(sdm::LayerRect *dst, struct Rect *src)
{
	dst->left = src->left;
	dst->top = src->top;
	dst->right = src->right;
	dst->bottom = src->bottom;
}

static void
SetRectArray(std::vector<sdm::LayerRect> &dst, struct RectArray *src)
{
	for (uint32_t i = 0; i < src->count; i++) {
		sdm::LayerRect visible_rect = {};
		SetRect(&visible_rect, &src->rects[i]);
		dst.push_back(visible_rect);
	}
}

static sdm::LayerBlending
GetSDMBlending(uint32_t source)
{
	sdm::LayerBlending blending = sdm::kBlendingPremultiplied;

	switch (source) {
		case PLUGIN_BLENDING_PREMULTIPLIED:
			blending = sdm::kBlendingPremultiplied;
			break;
		case PLUGIN_BLENDING_NONE:
		default:
			blending = sdm::kBlendingOpaque;
			break;
	}

	return blending;
}

static int
ConfigLayerStack(struct Display *d, struct PluginLayersConfig *configs)
{
	int error = PLUGIN_ERROR_NONE;
	uint32_t i;
	bool has_skip_layer = false, has_cursor_layer = false;
	sdm::LayerStack *layer_stack = d->hw_layers.info.stack;

	for (i = 0; i < configs->count; i++) {
		sdm::Layer *layer = layer_stack->layers.at(i);
		struct PluginLayerGeometry *layer_geometry = &configs->layers[i];
		sdm::LayerBuffer *layer_buffer = layer->input_buffer;

		/* 1. Fill buffer information */
		*layer_buffer = sdm::LayerBuffer();
		layer_buffer->format = PluginFormatToSDMFormat(layer_geometry->format, layer_geometry->flags);
		layer_buffer->width = layer_geometry->width;
		layer_buffer->height = layer_geometry->height;
		/* Reset buffer flags */
		layer_buffer->flags.flags = 0;
		/* TODO: Below information should be set according to the real user scenario */
		layer_buffer->flags.secure = false;
		layer_buffer->flags.video = false;
		layer_buffer->flags.macro_tile = false;
		layer_buffer->flags.interlace = false;
		layer_buffer->flags.secure_display = false;

		/* 2. Fill layer information */
		if (layer_geometry->composition == PLUGIN_COMPOSITION_FB_TARGET)
			layer->composition = sdm::kCompositionGPUTarget;
		else
			layer->composition = sdm::kCompositionGPU;

		SetRect(&layer->src_rect, &layer_geometry->src_rect);
		SetRect(&layer->dst_rect, &layer_geometry->dst_rect);
		SetRectArray(layer->visible_regions, &layer_geometry->visible_regions);
		SetRectArray(layer->dirty_regions, &layer_geometry->dirty_regions);

		layer->blending = GetSDMBlending(layer_geometry->blending);
		layer->plane_alpha = layer_geometry->plane_alpha;
		layer->transform.flip_horizontal = ((layer_geometry->transform & PLUGIN_TRANSFORM_FLIP_H) > 0);
		layer->transform.flip_vertical = ((layer_geometry->transform & PLUGIN_TRANSFORM_FLIP_V) > 0);
		layer->transform.rotation = ((layer_geometry->transform & PLUGIN_TRANSFORM_90) ? 90.0f : 0.0f);

		layer->frame_rate = d->attribs.fps;
		layer->solid_fill_color = 0;

		/* Reset layer flags */
		layer->flags.flags = 0;

		layer->flags.skip = layer_geometry->flags.skip;
		layer->flags.cursor = layer_geometry->flags.is_cursor;

		if (!has_skip_layer && layer->flags.skip) {
			has_skip_layer = true;
		}
		if (!has_cursor_layer && layer->flags.cursor) {
			has_cursor_layer = true;
		}
		layer->flags.updating = true;
		layer->flags.solid_fill = false;
		layer->flags.single_buffer = false;
	}

	/* Reset flags */
	layer_stack->flags.flags = 0;

	/* 3. Update layerstack flags */
	if (has_skip_layer)
		layer_stack->flags.skip_present = true;
	if (has_cursor_layer)
		layer_stack->flags.cursor_present = true;
	/* TODO: add retire fence, output buffer if they are supported in future */
	/* TODO: Modify below flags according to the real user scenario */
	layer_stack->flags.geometry_changed = true;
	layer_stack->flags.video_present = false;
	layer_stack->flags.secure_present = false;
	layer_stack->flags.animating = false;
	layer_stack->flags.attributes_changed = false;
	layer_stack->flags.single_buffered_layer_present = false;
	layer_stack->flags.s3d_mode_present = false;
	layer_stack->flags.post_processed_output = false;

	return error;
}

static int
StrategyMgrPrepare(struct Display *d, bool partial_update_enable)
{
	struct StrategyContext *strategy_ctx = &d->ctx.strategy_ctx;
	sdm::PartialUpdateImpl *partial_update_impl = strategy_ctx->partial_update_impl;
	sdm::HWLayersInfo *hw_layers_info = &d->hw_layers.info;
	sdm::LayerStack *layer_stack = hw_layers_info->stack;
	uint32_t i = 0, fb_layer_index, layer_count = 0;
	bool split_display = false;

	layer_count = layer_stack->layers.size();
	for (; i < layer_count; i++) {
		sdm::Layer *layer = layer_stack->layers.at(i);
		if (layer->composition == sdm::kCompositionGPUTarget) {
			fb_layer_index = i;
			break;
		}
	}

	if (i == layer_count) {
		SDM_PLUGIN_LOGE("no FB target!\n");
		return PLUGIN_ERROR_UNDEFINED;
	}

	if (partial_update_impl) {
		partial_update_impl->ControlPartialUpdate(partial_update_enable);
	}

	/* Generate ROI */
	if(partial_update_impl == NULL ||
		(partial_update_impl->GenerateROI(hw_layers_info) != sdm::kErrorNone)) {
		sdm::Layer *layer = layer_stack->layers.at(fb_layer_index);
		sdm::LayerRect &dst_rect = layer->dst_rect;
		/* The destination co-ordinates of the FB layer map to the panel and may be different than source */
		float fb_x_res = dst_rect.right - dst_rect.left;
		float fb_y_res = dst_rect.bottom - dst_rect.top;

		if (!global_plugin_ptr->hw_res_info.is_src_split &&
			((fb_x_res > global_plugin_ptr->hw_res_info.max_mixer_width) ||
			((d->ctx.type == sdm::kPrimary) && d->panel.split_info.right_split))) {
			split_display = true;
		}

		if (split_display) {
			float left_split = FLOAT(d->panel.split_info.left_split);
			hw_layers_info->left_partial_update = (sdm::LayerRect) {0.0f, 0.0f, left_split, fb_y_res};
			hw_layers_info->right_partial_update = (sdm::LayerRect) {left_split, 0.0f, fb_x_res, fb_y_res};
		} else {
			hw_layers_info->left_partial_update = (sdm::LayerRect) {0.0f, 0.0f, fb_x_res, fb_y_res};
			hw_layers_info->right_partial_update = (sdm::LayerRect) {0.0f, 0.0f, 0.0f, 0.0f};
		}
	}

	return PLUGIN_ERROR_NONE;
}

static int
StrategyMgrStart(struct StrategyContext *ctx,
		sdm::HWLayersInfo *info)
{
	sdm::StrategyImpl *strategy_impl = ctx->strategy_impl;
	sdm::DisplayError error = sdm::kErrorNone;

	ctx->start_success = false;
	ctx->tried_default = false;

	/* Start strategy manager */
	if (strategy_impl) {
		error = strategy_impl->Start(info, &ctx->max_strategies);
		if (error == sdm::kErrorNone)
			ctx->start_success = true;
		else
			ctx->max_strategies = 1;
	}

	return PLUGIN_ERROR_NONE;
}

static int
StrategyMgrStop(struct StrategyContext *ctx)
{
	sdm::StrategyImpl *strategy_impl = ctx->strategy_impl;
	sdm::DisplayError error = sdm::kErrorNone;

	if (ctx->start_success)
		error = strategy_impl->Stop();

	if (error != sdm::kErrorNone) {
		SDM_PLUGIN_LOGE("fail to stop strategy manager\n");
		return PLUGIN_ERROR_SDM;
	}

	return PLUGIN_ERROR_NONE;
}

static bool
SupportLayerAsCursor(sdm::Handle disp_res_ctx,
			sdm::HWLayers *hw_layers)
{
	sdm::ResourceImpl *res_impl = global_plugin_ptr->res_impl;
	sdm::LayerStack *layer_stack = hw_layers->info.stack;
	bool supported = false;
	uint32_t i, layer_count = 0;

	if (!layer_stack->flags.cursor_present) {
		return supported;
	}

	layer_count = layer_stack->layers.size();
	/*
	 * SDM logic is checking gpu_index - 1, why we need to do this?
	 * Just check if is_cursor is set to true.
	 */
	for (i = 0; i < layer_count; i++) {
		sdm::Layer *layer = layer_stack->layers.at(i);
		if (layer->flags.cursor) {
			break;
		}
	}
	if (i == layer_count) {
		return supported;
	}
	sdm::Layer *cursor_layer = layer_stack->layers.at(i);
	if (res_impl->ValidateCursorConfig(disp_res_ctx,
					cursor_layer, true) == sdm::kErrorNone) {
		supported = true;
	}

	return supported;
}

static void
InitStrategyConstrains(struct DisplayContext *ctx,
			sdm::HWLayers *hw_layers)
{
	sdm::StrategyConstraints *constraints = &ctx->constraints;

	/* TODO: set safe_mode according to user sceneraios */
	constraints->safe_mode = false;
	constraints->use_cursor = false;
	constraints->max_layers = ctx->max_layers ? ctx->max_layers : sdm::kMaxSDELayers;

	/*
	 * Avoid idle fallback, if there is only one app layer.
	 * TODO(user): App layer count will change for hybrid composition
	 */
	uint32_t app_layer_count = hw_layers->info.stack->layers.size() - 1;
	if ((app_layer_count > 1 && ctx->idle_fallback) || ctx->fallback) {
		/* Handle the idle timeout by falling back */
		constraints->safe_mode = true;
	}

	if (SupportLayerAsCursor(ctx->disp_res_ctx, hw_layers)) {
		constraints->use_cursor = true;
	}
}

static sdm::DisplayError
GetNextStrategy(struct StrategyContext *ctx,
		sdm::HWLayersInfo *info,
		sdm::StrategyConstraints *constraints)
{
	sdm::StrategyImpl *strategy_impl = ctx->strategy_impl;
	sdm::DisplayError error = sdm::kErrorNone;
	uint32_t i;

	if (ctx->start_success) {
		error = strategy_impl->GetNextStrategy(constraints);
		if (error == sdm::kErrorNone)
			return error;
	}

	/* Default composition is already tried. */
	if (ctx->tried_default)
		return sdm::kErrorUndefined;

	sdm::LayerStack *layer_stack = info->stack;
	uint32_t &hw_layers_count = info->count;
	hw_layers_count = 0;

	/*
	 * Mark all application layers for GPU composition. Find GPU target buffer and store its index for
	 * programming the hardware
	 */
	for (i = 0; i < layer_stack->layers.size(); i++) {
		sdm::Layer *layer = layer_stack->layers.at(i);
		sdm::LayerComposition &composition = layer->composition;
		if (composition == sdm::kCompositionGPUTarget) {
			info->index[hw_layers_count++] = i;
		} else if (composition != sdm::kCompositionBlitTarget) {
			composition = sdm::kCompositionGPU;
		}
	}

	ctx->tried_default = true;
	/* There can be one and only one GPU target buffer. */
	if (hw_layers_count != 1) {
		SDM_PLUGIN_LOGE("more than one fb target\n");
		return sdm::kErrorParameters;
	}

	return sdm::kErrorNone;
}

static int
DoPrepare (struct Display *d, bool partial_update_enable)
{
	struct StrategyContext *strategy_ctx = &d->ctx.strategy_ctx;
	sdm::ResourceImpl *res_impl = global_plugin_ptr->res_impl;
	sdm::StrategyConstraints *constrains = &d->ctx.constraints;
	sdm::HWLayers *hw_layers = &d->hw_layers;
	sdm::Handle disp_res_ctx = d->ctx.disp_res_ctx;
	sdm::DisplayError error = sdm::kErrorNone;
	bool exit = false;
	int count;

	/* 1. Prepare work such Generate ROI */
	StrategyMgrPrepare(d, partial_update_enable);

	/* 2. start strategy manager */
	StrategyMgrStart(strategy_ctx, &hw_layers->info);

	/* 3. initialize strategy constrains */
	InitStrategyConstrains(&d->ctx, hw_layers);

	/* 4. start resource manager */
	res_impl->Start(disp_res_ctx);

	/* 5. do strategy filter */
	for (count = strategy_ctx->max_strategies; !exit && count > 0; count--) {
		error = GetNextStrategy(strategy_ctx, &hw_layers->info, constrains);
		if (error != sdm::kErrorNone)
			exit = true;

		if (!exit) {
			error = res_impl->Acquire(disp_res_ctx, hw_layers);
			exit = (error == sdm::kErrorNone);
		}
	}
	if (error != sdm::kErrorNone)
		SDM_PLUGIN_LOGE("Composition strategies exhausted for display=%p\n", d);

	/* 6. stop resource manager */
	res_impl->Stop(disp_res_ctx);

	/* 7. post prepare for resource manager. Do nothing now! */
	error = res_impl->PostPrepare(disp_res_ctx, hw_layers);
	if (error != sdm::kErrorNone)
		SDM_PLUGIN_LOGE("Post prepare failed for display=%p\n", d);

	/* 8. stop strategy manager */
	StrategyMgrStop(strategy_ctx);

	return (error != sdm::kErrorNone) ? PLUGIN_ERROR_SDM : PLUGIN_ERROR_NONE;
}

static uint32_t
GetComposition(sdm::LayerComposition composition)
{
	uint32_t ret;

	switch (composition) {
		case sdm::kCompositionGPUTarget:
			ret = PLUGIN_COMPOSITION_FB_TARGET;
			break;
		case sdm::kCompositionGPU:
			ret = PLUGIN_COMPOSITION_GPU;
			break;
		case sdm::kCompositionHWCursor:
			ret = PLUGIN_COMPOSITION_HW_CURSOR;
			break;
		default:
			ret = PLUGIN_COMPOSITION_OVERLAY;
			break;
	}

	return ret;
}

static void
GetSDMLayerConfig(struct sdmLayerConfig *out_config, sdm::HWLayerConfig *hw_layer_config)
{
	sdm::HWPipeInfo *left_pipe = &hw_layer_config->left_pipe;
	sdm::HWPipeInfo *right_pipe = &hw_layer_config->right_pipe;
	struct sdmPipeInfo *out_left_pipe = &out_config->left_pipe;
	struct sdmPipeInfo *out_right_pipe = &out_config->right_pipe;

	for (uint32_t i = 0; i < 2; i++) {
		sdm::HWPipeInfo *pipe_info = (i == 0) ? left_pipe : right_pipe;
		struct sdmPipeInfo *out_pipe = (i == 0) ? out_left_pipe : out_right_pipe;

		if (!pipe_info->valid)
			continue;

		out_pipe->valid = pipe_info->valid;
		out_pipe->pipe_id = pipe_info->pipe_id;
		out_pipe->z_order = pipe_info->z_order;

		out_pipe->src_roi.left = pipe_info->src_roi.left;
		out_pipe->src_roi.right = pipe_info->src_roi.right;
		out_pipe->src_roi.top = pipe_info->src_roi.top;
		out_pipe->src_roi.bottom = pipe_info->src_roi.bottom;
		out_pipe->dst_roi.left = pipe_info->dst_roi.left;
		out_pipe->dst_roi.right = pipe_info->dst_roi.right;
		out_pipe->dst_roi.top = pipe_info->dst_roi.top;
		out_pipe->dst_roi.bottom = pipe_info->dst_roi.bottom;

		out_pipe->h_decimation = pipe_info->horizontal_decimation;
		out_pipe->v_decimation = pipe_info->vertical_decimation;
	}
}

static void
UpdateLayerConfig(sdm::HWLayers *hw_layers, struct PluginLayersConfig *config)
{
	sdm::HWLayersInfo *info = &hw_layers->info;

	for (uint32_t i = 0; i < config->count; i++) {
		sdm::LayerStack *stack = info->stack;
		sdm::Layer *layer = stack->layers.at(i);

		config->layers[i].composition = GetComposition(layer->composition);

		GetSDMLayerConfig(&config->sdm_layer_configs[i], &hw_layers->config[i]);
	}
}

int
SDMPluginPrepare(client_handle_t handle, display_handle_t displays[], int count, struct PluginLayersConfig **configs, uint32_t flags)
{
	struct StrategyClient *client = static_cast<struct StrategyClient *>(handle);
	int error = PLUGIN_ERROR_NONE;
	int i, index;

	PluginLock();

	if(!IsInitialized()) {
		SDM_PLUGIN_LOGE("plugin is not initialized\n");
		PluginUnlock();
		return PLUGIN_ERROR_MEMORY;
	}

	if (!client || !displays || !configs || count == 0) {
		SDM_PLUGIN_LOGE("invalid parameter\n");
		PluginUnlock();
		return PLUGIN_ERROR_PARAMETER;
	}

	index = FindClientIndex(client);
	if (index == -1) {
		PluginUnlock();
		return PLUGIN_ERROR_PARAMETER;
	}

	/* Give a chance to other displays if one of them fails to do prepare */
	for (i = 0; i < count; i++) {
		struct Display *d = static_cast<struct Display *>(displays[i]);
		index = FindDisplaySlot(client, d);
		if(index == -1)
			continue;

		if(configs[i] == NULL) {
			SDM_PLUGIN_LOGE("Null config for display%d!\n", i);
			continue;
		}

		error = AllocLayerStackMemory(d, configs[i]);
		if (error) {
			SDM_PLUGIN_LOGE("alloc layer stack fail for display%d!\n", i);
			continue;
		}

		d->hw_layers.info = sdm::HWLayersInfo();
		d->hw_layers.info.stack = &d->layer_stack;
		d->hw_layers.output_compression = 1.0f;

		error = ConfigLayerStack(d, configs[i]);
		if (error) {
			SDM_PLUGIN_LOGE("config layer stack fail for display%d\n", i);
			continue;
		}

		/* do strategy filter */
		error = DoPrepare(d, false);
		if (error) {
			SDM_PLUGIN_LOGE("fail to filter strategy for display%d\n", i);
			continue;
		}
		/* Update composition for configs */
		UpdateLayerConfig(&d->hw_layers, configs[i]);
	}

	PluginUnlock();
	return error;
}

int
SDMPluginCommit(client_handle_t handle, display_handle_t displays[], int count, uint32_t flags)
{
	struct StrategyClient *client = static_cast<struct StrategyClient *>(handle);
	struct Display *d = NULL;
	int i, index;
	sdm::DisplayError error = sdm::kErrorNone;

	PluginLock();

	if(!IsInitialized()) {
		SDM_PLUGIN_LOGE("plugin is not initialized\n");
		PluginUnlock();
		return PLUGIN_ERROR_MEMORY;
	}

	if (!client || !displays || count == 0) {
		SDM_PLUGIN_LOGE("invalid parameter\n");
		PluginUnlock();
		return PLUGIN_ERROR_PARAMETER;
	}

	index = FindClientIndex(client);
	if (index == -1) {
		PluginUnlock();
		return PLUGIN_ERROR_PARAMETER;
	}

	for (i = 0; i < count; i++) {
		d = static_cast<struct Display *>(displays[i]);
		index = FindDisplaySlot(client, d);
		if(index == -1) {
			PluginUnlock();
			return PLUGIN_ERROR_PARAMETER;
		}
	}

	/* TODO:How to rollback if there is failure for display? */
	for (i = 0; i < count; i++) {
		d = static_cast<struct Display *>(displays[i]);
		error = global_plugin_ptr->res_impl->PostCommit(
					d->ctx.disp_res_ctx,
					&d->hw_layers);
		if (error != sdm::kErrorNone) {
			SDM_PLUGIN_LOGE("fail to commit display%d\n", i);
			continue;
		}
	}

	PluginUnlock();
	return PLUGIN_ERROR_NONE;
}

static bool
IsValidDisplayId(int id)
{
	return ((1 << id) & global_plugin_ptr->displays_mask);
}

static bool
IsDisplayOp(uint32_t opcode)
{
	return (opcode == PLUGIN_UPDATE_DISPLAY_ATTRIBUTES ||
		opcode == PLUGIN_UPDATE_PANEL_INFO ||
		opcode == PLUGIN_UPDATE_IDLE_TIME ||
		opcode == PLUGIN_UPDATE_HW_LAYERS ||
		opcode == PLUGIN_UPDATE_THERMAL_LEVEL ||
		opcode == PLUGIN_UPDATE_MAX_MIXER_STAGES ||
		opcode == PLUGIN_UPDATE_PARTIAL_UPDATE ||
		opcode == PLUGIN_VALIDATE_CURSOR_POSITION ||
		opcode == PLUGIN_QUERY_CAN_SET_IDLE_TIME ||
		opcode == PLUGIN_PURGE_DISPLAY_RESOURCE);
}

static bool
IsUpdateOp(uint32_t opcode)
{
	return (opcode == PLUGIN_UPDATE_DISPLAY_ATTRIBUTES ||
		opcode == PLUGIN_UPDATE_PANEL_INFO ||
		opcode == PLUGIN_UPDATE_IDLE_TIME ||
		opcode == PLUGIN_UPDATE_THERMAL_LEVEL ||
		opcode == PLUGIN_UPDATE_MAX_MIXER_STAGES ||
		opcode == PLUGIN_UPDATE_PARTIAL_UPDATE ||
		opcode == PLUGIN_UPDATE_BW_MODE ||
		opcode == PLUGIN_UPDATE_HW_RESOURCE ||
		opcode == PLUGIN_PURGE_DISPLAY_RESOURCE);
}

static bool
IsValidateOp(uint32_t opcode)
{
	return (opcode == PLUGIN_VALIDATE_SCALING ||
		opcode == PLUGIN_VALIDATE_CURSOR_POSITION);
}

static bool
IsQueryOp(uint32_t opcode)
{
	return (opcode == PLUGIN_QUERY_CAN_SET_IDLE_TIME);
}

/* Only property masked in masks will be flushed */
static int
FlushGlobalProperty(struct GlobalProperty *prop, uint32_t masks)
{
	masks &= PLUGIN_PROPERTIES_MASK;

	if (!masks)
		return PLUGIN_ERROR_NONE;

	if (masks & PLUGIN_UPDATE_HW_RESOURCE_MASK) {
		/* TODO: How to define the behavior of it? */
		prop->dirty_bits &= ~PLUGIN_UPDATE_HW_RESOURCE_MASK;
	}

	if (masks & PLUGIN_UPDATE_BW_MODE_MASK) {
		global_plugin_ptr->res_impl->SetMaxBandwidthMode((sdm::HWBwModes)prop->max_bw_mode);
		prop->dirty_bits &= ~PLUGIN_UPDATE_BW_MODE_MASK;
	}

	return PLUGIN_ERROR_NONE;
}

static int
FlushDisplayProperty(struct Display *display, struct DisplayProperty *prop, uint32_t masks)
{
	sdm::ResourceImpl *res_impl = global_plugin_ptr->res_impl;
	struct DisplayContext *ctx = &display->ctx;
	sdm::Handle disp_res_ctx = ctx->disp_res_ctx;
	bool need_reset_strategy = false;

	masks &= PLUGIN_PROPERTIES_MASK;
	if (!masks)
		return PLUGIN_ERROR_NONE;

	if(masks & PLUGIN_UPDATE_DISPLAY_ATTRIBUTES_MASK) {
		GetDisplayAttribs(&prop->attribs, display->attribs);
		need_reset_strategy = true;
		prop->dirty_bits &= ~PLUGIN_UPDATE_DISPLAY_ATTRIBUTES_MASK;
	}

	if(masks & PLUGIN_UPDATE_PANEL_INFO_MASK) {
		GetPanelInfo(&prop->panel, display->panel);
		need_reset_strategy = true;
		prop->dirty_bits &= ~PLUGIN_UPDATE_PANEL_INFO_MASK;
	}

	if(masks & PLUGIN_UPDATE_IDLE_TIME_MASK) {
		ctx->idle_fallback = true;
		prop->dirty_bits &= ~PLUGIN_UPDATE_IDLE_TIME_MASK;
	}

	if (masks & PLUGIN_UPDATE_HW_LAYERS_MASK) {
		/* TODO: How to do it? Because prepare may have already updated HWLayers */
		prop->dirty_bits &= ~PLUGIN_UPDATE_HW_LAYERS_MASK;
	}

	if (masks & PLUGIN_UPDATE_THERMAL_LEVEL_MASK) {
		if (prop->thermal_level >= PLUGIN_MAX_THERMAL_LEVEL)
			ctx->fallback = true;
		else
			ctx->fallback = false;
		prop->dirty_bits &= ~PLUGIN_UPDATE_THERMAL_LEVEL_MASK;
	}

	if (masks & PLUGIN_UPDATE_MAX_MIXER_STAGES_MASK) {
		res_impl->SetMaxMixerStages(disp_res_ctx, prop->max_mixer_stages);
		prop->dirty_bits &= ~PLUGIN_UPDATE_MAX_MIXER_STAGES_MASK;
	}

	if (masks & PLUGIN_UPDATE_PARTIAL_UPDATE_MASK) {
		ctx->partial_update_enable = prop->partial_update_enable;
		prop->dirty_bits &= ~PLUGIN_UPDATE_PARTIAL_UPDATE_MASK;
	}

	if (masks & PLUGIN_PURGE_DISPLAY_RESOURCE_MASK) {
		res_impl->Purge(disp_res_ctx);
		prop->dirty_bits &= ~PLUGIN_PURGE_DISPLAY_RESOURCE_MASK;
	}

	/* Reset strategy manager since display attributes/panel information are already updated */
	if (need_reset_strategy) {
		struct StrategyContext *strategy_ctx = &display->ctx.strategy_ctx;
		uint32_t error = PLUGIN_ERROR_NONE;

		res_impl->ReconfigureDisplay(disp_res_ctx, display->attribs, display->panel, display->mixer_attributes);

		DestroyStrategyMgr(strategy_ctx);

		/* Update fb config */
		GetFBConfig(&display->attribs, &display->mixer_attributes, display->fb_config);

		error = CreateStrategyMgr(strategy_ctx, display->ctx.type,
				global_plugin_ptr->hw_res_info,
				display->attribs,
				display->panel,
				display->mixer_attributes,
				display->fb_config);
		if (error != PLUGIN_ERROR_NONE) {
			SDM_PLUGIN_LOGE("fail to create strategy manager\n");
			return error;
		}
	}

	return PLUGIN_ERROR_NONE;
}

/* This function will not set bitmask but apply property immediately. */
static void
FlushOneProperty(struct StrategyClient *client, struct Property *prop)
{
	uint32_t opcode = prop->opcode;

	if (IsDisplayOp(opcode)) {
		uint32_t id = prop->data.disp_prop.id;
		struct Display *display = client->displays[id];
		struct DisplayProperty *prop = &client->prop_state.disp_props[id];

		FlushDisplayProperty(display, prop, 1<<opcode);
	} else {
		struct GlobalProperty *prop = &client->prop_state.global_props;

		FlushGlobalProperty(prop, 1<<opcode);
	}
}

/* Update property to plugin property state. */
static int
DoUpdateProperty(struct StrategyClient *client, struct Property *prop)
{
	struct PropertyState *s = &client->prop_state;
	uint32_t opcode = prop->opcode;
	PropertyData *data = &prop->data;
	uint32_t id = 0;

	if (!IsUpdateOp(opcode)) {
		SDM_PLUGIN_LOGE("invalid update property code\n");
		return PLUGIN_ERROR_UNDEFINED;
	}

	if (IsDisplayOp(opcode)) {
		id = data->disp_prop.id;
		if (!IsValidDisplayId(id)) {
			SDM_PLUGIN_LOGE("invalid display id %d for display propery\n", id);
			return PLUGIN_ERROR_PARAMETER;
		}

		if(!client->displays[id]) {
			SDM_PLUGIN_LOGE("display %d is not created or already released!\n", id);
			return PLUGIN_ERROR_PARAMETER;
		}
	}

	/* Some property doesn't modify value but launch an operation. If so, just do nothing. */
	switch (opcode) {
		case PLUGIN_UPDATE_DISPLAY_ATTRIBUTES:
			s->disp_props[id].attribs = data->disp_prop.attribs;
			break;
		case PLUGIN_UPDATE_PANEL_INFO:
			s->disp_props[id].panel = data->disp_prop.panel;
			break;
		case PLUGIN_UPDATE_HW_LAYERS:
			s->disp_props[id].layer_configs = data->disp_prop.layer_configs;
			break;
		case PLUGIN_UPDATE_THERMAL_LEVEL:
			s->disp_props[id].thermal_level = data->disp_prop.thermal_level;
			break;
		case PLUGIN_UPDATE_MAX_MIXER_STAGES:
			s->disp_props[id].max_mixer_stages = data->disp_prop.max_mixer_stages;
			break;
		case PLUGIN_UPDATE_PARTIAL_UPDATE:
			s->disp_props[id].partial_update_enable = data->disp_prop.partial_update_enable;
			break;
		case PLUGIN_UPDATE_BW_MODE:
			s->global_props.max_bw_mode = data->max_bw_mode;
			break;
		case PLUGIN_UPDATE_HW_RESOURCE:
			s->global_props.hw_res = data->hw_res;
			break;
		case PLUGIN_UPDATE_IDLE_TIME:
		case PLUGIN_PURGE_DISPLAY_RESOURCE:
			break;
		default:
			/* Should not happen! */
			SDM_PLUGIN_LOGE("unknown property code %d\n", opcode);
			break;
	}

	/* Update dirty mask */
	if (IsDisplayOp(opcode)) {
		s->disp_props[id].dirty_bits |= 1<<opcode;
	} else {
		s->global_props.dirty_bits |= 1<<opcode;
	}

	return PLUGIN_ERROR_NONE;
}

int
SDMPluginUpdateProperty(client_handle_t handle, struct Property *prop, bool flush, uint32_t flags)
{
	struct StrategyClient *client = static_cast<struct StrategyClient *>(handle);
	uint32_t error = PLUGIN_ERROR_NONE;

	PluginLock();

	if(!IsInitialized()) {
		SDM_PLUGIN_LOGE("plugin is not initialized\n");
		PluginUnlock();
		return PLUGIN_ERROR_MEMORY;
	}

	if (!prop || !client) {
		SDM_PLUGIN_LOGE("NULL parameter prop %p client %p\n", prop, client);
		PluginUnlock();
		return PLUGIN_ERROR_PARAMETER;
	}

	/* Update property to cache state */
	error = DoUpdateProperty(client, prop);
	if (error) {
		SDM_PLUGIN_LOGE("fail to update property!\n");
		PluginUnlock();
		return error;
	}

	if (flush) {
		/* Flush property to SDM. This may update some values in DisplayContext */
		FlushOneProperty(client, prop);
	}

	PluginUnlock();
	return PLUGIN_ERROR_NONE;
}

int
SDMPluginQueryProperty(client_handle_t handle, struct Property *prop, uint32_t flags)
{
	struct StrategyClient *client = static_cast<struct StrategyClient *>(handle);
	struct Display *display = NULL;
	uint32_t id = 0;

	PluginLock();

	if(!IsInitialized()) {
		SDM_PLUGIN_LOGE("plugin is not initialized\n");
		PluginUnlock();
		return PLUGIN_ERROR_MEMORY;
	}

	if (!prop || !client) {
		SDM_PLUGIN_LOGE("NULL parameter prop %p client %p\n", prop, client);
		PluginUnlock();
		return PLUGIN_ERROR_PARAMETER;
	}

	if (IsQueryOp(prop->opcode)) {
		SDM_PLUGIN_LOGE("invalid opcode %d\n", prop->opcode);
		PluginUnlock();
		return PLUGIN_ERROR_UNDEFINED;
	}

	if (IsDisplayOp(prop->opcode)) {
		id = prop->data.disp_prop.id;
		if (!IsValidDisplayId(id)) {
			SDM_PLUGIN_LOGE("invalid display id %d\n", id);
			PluginUnlock();
			return PLUGIN_ERROR_PARAMETER;
		}
		display = client->displays[id];
	}

	switch (prop->opcode) {
		case PLUGIN_QUERY_CAN_SET_IDLE_TIME:
			prop->data.disp_prop.is_idle = (display != NULL) &&
						(display->ctx.idle_fallback == false);
			break;
		default:
			break;
	}

	PluginUnlock();
	return PLUGIN_ERROR_NONE;
}

int
SDMPluginValidateProperty(client_handle_t handle, struct Property *prop, uint32_t flags)
{
	struct StrategyClient *client = static_cast<struct StrategyClient *>(handle);
	struct Display *display = NULL;
	sdm::ResourceImpl *res_impl = global_plugin_ptr->res_impl;
	sdm::DisplayError sdm_err = sdm::kErrorNone;
	sdm::LayerRect src, dst;
	uint32_t error = PLUGIN_ERROR_NONE;
	uint32_t id = 0;

	PluginLock();

	if(!IsInitialized()) {
		SDM_PLUGIN_LOGE("plugin is not initialized\n");
		error = PLUGIN_ERROR_MEMORY;
		goto error_validation;
	}

	if (!prop || !client) {
		SDM_PLUGIN_LOGE("NULL parameter prop %p client %p\n", prop, client);
		error = PLUGIN_ERROR_PARAMETER;
		goto error_validation;
	}

	if (IsValidateOp(prop->opcode)) {
		SDM_PLUGIN_LOGE("invalid opcode %d\n", prop->opcode);
		error = PLUGIN_ERROR_UNDEFINED;
		goto error_validation;
	}

	if (IsDisplayOp(prop->opcode)) {
		id = prop->data.disp_prop.id;
		if (!IsValidDisplayId(id)) {
			SDM_PLUGIN_LOGE("invalid display id %d\n", id);
			error = PLUGIN_ERROR_PARAMETER;
			goto error_validation;
		}
		if (!client->displays[id]) {
			SDM_PLUGIN_LOGE("display %d is not created or already released!\n", id);
			error = PLUGIN_ERROR_NOT_SUPPORTED;
			goto error_validation;
		}
		display = client->displays[id];
	}

	switch (prop->opcode) {
		case PLUGIN_VALIDATE_SCALING:
			SetRect(&src, &prop->data.scaling.src);
			SetRect(&dst, &prop->data.scaling.dst);
			/* TODO: set correct value for ubwc and downscaling */
			sdm_err =
				res_impl->ValidateScaling(src, dst, prop->data.scaling.rotate_90, false, true);
			if (sdm_err != sdm::kErrorNone) {
				SDM_PLUGIN_LOGE("fail to validate scaling\n");
				error = PLUGIN_ERROR_SDM;
			}
			break;
		case PLUGIN_VALIDATE_CURSOR_POSITION:
			int x, y;
			x = prop->data.disp_prop.cur_pos.x;
			y = prop->data.disp_prop.cur_pos.y;
			sdm_err =
				res_impl->ValidateCursorPosition(display->ctx.disp_res_ctx, &display->hw_layers, x, y);
			if (sdm_err != sdm::kErrorNone) {
				SDM_PLUGIN_LOGE("fail to validate cursor position\n");
				error = PLUGIN_ERROR_SDM;
			}
			break;
		default:
			break;
	}

error_validation:
	PluginUnlock();
	return error;
}

int
SDMPluginApplyProperty(client_handle_t handle, uint32_t flags)
{
	struct StrategyClient *client = static_cast<struct StrategyClient *>(handle);
	struct PropertyState *state = NULL;

	PluginLock();

	if(!IsInitialized()) {
		SDM_PLUGIN_LOGE("plugin is not initialized\n");
		PluginUnlock();
		return PLUGIN_ERROR_MEMORY;
	}

	if (!client) {
		SDM_PLUGIN_LOGE("client handle can't be NULL\n");
		PluginUnlock();
		return PLUGIN_ERROR_PARAMETER;
	}

	state = &client->prop_state;

	/* Flush global properties. This need to be handled first because maybe HWResourceInfo is modified */
	struct GlobalProperty * global_props = &state->global_props;
	FlushGlobalProperty(global_props, global_props->dirty_bits);

	/* Flush display propertys */
	for (uint32_t i = 0; i < PLUGIN_MAX_DISPLAYS; i++) {
		struct DisplayProperty *disp_props = &state->disp_props[i];
		struct Display *display = client->displays[i];
		if (!display) {
			SDM_PLUGIN_LOGE("display %d has not been created yet\n", i);
			continue;
		}

		FlushDisplayProperty(display, disp_props, disp_props->dirty_bits);
	}

	PluginUnlock();
	return PLUGIN_ERROR_NONE;
}

int
SDMPluginDeinit(plugin_handle_t handle)
{
	struct StrategyPlugin *strategy_plugin = static_cast<struct StrategyPlugin *>(handle);
	struct StrategyClientContext *client_ctx = NULL;
	int i, j;

	PluginLock();

	if (!strategy_plugin || strategy_plugin != global_plugin_ptr) {
		PluginUnlock();
		return PLUGIN_ERROR_PARAMETER;
	}

	if (!IsInitialized()) {
		PluginUnlock();
		return PLUGIN_ERROR_NONE;
	}

	client_ctx = &strategy_plugin->client_ctx;
	for (i = 0; i < MAX_STRATEGY_CLIENTS; i++) {
		struct StrategyClient *client = client_ctx->clients[i];
		if (!client)
			continue;

		for (j = 0; j < PLUGIN_MAX_DISPLAYS; j++) {
			struct Display *d = client->displays[j];
			if (d) {
				DoDestroyDisplay(d);
				client->displays[j] = NULL;
			}
		}

		delete client;
		client_ctx->clients[i] = NULL;
		if (client_ctx->count == 0) {
			SDM_PLUGIN_LOGE("fatal error! client count is not matched with exist clients!\n");
		} else {
			client_ctx->count--;
		}
	}

	if (strategy_plugin->res_impl) {
		strategy_plugin->res_impl->Deinit();
		delete strategy_plugin->res_impl;
		strategy_plugin->res_impl = NULL;
	}

	if (strategy_plugin->buf_sync_handler) {
		delete strategy_plugin->buf_sync_handler;
		strategy_plugin->buf_sync_handler = NULL;
	}

	/* Maybe no need to do this */
	strategy_plugin->hw_res_info.Reset();
	strategy_plugin->is_init = false;

	PluginLogDeinit();

	PluginUnlock();
	return PLUGIN_ERROR_NONE;
}

struct StrategyPluginInterface StrategyPluginInterface = {
	.Init = SDMPluginInit,
	.CreateClient = SDMPluginCreateClient,
	.DestroyClient = SDMPluginDestroyClient,
	.CreateDisplay = SDMPluginCreateDisplay,
	.DestroyDisplay = SDMPluginDestroyDisplay,
	.Prepare = SDMPluginPrepare,
	.Commit = SDMPluginCommit,
	.UpdateProperty = SDMPluginUpdateProperty,
	.QueryProperty = SDMPluginQueryProperty,
	.ValidateProperty = SDMPluginValidateProperty,
	.ApplyProperty = SDMPluginApplyProperty,
	.Deinit = SDMPluginDeinit
};

#ifdef __cplusplus
}
#endif
