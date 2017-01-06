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


#ifndef __SDM_STRATEGY_PLUGIN_INTERFACE_H__
#define __SDM_STRATEGY_PLUGIN_INTERFACE_H__

#define MAX_SDE_Layers          16
#define MAX_PIPE_WIDTH          2560
#define MAX_MIXER_WIDTH         2560

/* Error enum */
enum {
	PLUGIN_ERROR_NONE,
	PLUGIN_ERROR_UNINITIALIZED,
	PLUGIN_ERROR_MEMORY,
	PLUGIN_ERROR_PARAMETER,
	PLUGIN_ERROR_UNDEFINED,
	PLUGIN_ERROR_NOT_SUPPORTED,
	PLUGIN_ERROR_SDM,
};

/* Buffer format enum */
enum {
	PLUGIN_BUFFER_FORMAT_ARGB_8888,
	PLUGIN_BUFFER_FORMAT_RGBA_8888,
	PLUGIN_BUFFER_FORMAT_BGRA_8888,
	PLUGIN_BUFFER_FORMAT_XRGB_8888,
	PLUGIN_BUFFER_FORMAT_RGBX_8888,
	PLUGIN_BUFFER_FORMAT_BGRX_8888,
	PLUGIN_BUFFER_FORMAT_RGBA_5551,
	PLUGIN_BUFFER_FORMAT_RGBA_4444,
	PLUGIN_BUFFER_FORMAT_RGB_888,
	PLUGIN_BUFFER_FORMAT_BGR_888,
	PLUGIN_BUFFER_FORMAT_RGB_565,
	PLUGIN_BUFFER_FORMAT_BGR_565,
	PLUGIN_BUFFER_FORMAT_RGBA_8888_Ubwc,
	PLUGIN_BUFFER_FORMAT_RGBX_8888_Ubwc,
	PLUGIN_BUFFER_FORMAT_BGR565_Ubwc,
	PLUGIN_BUFFER_FORMAT_YCbCr_420_P,
	PLUGIN_BUFFER_FORMAT_YCrCb_420_P,
	PLUGIN_BUFFER_FORMAT_YV12,
	PLUGIN_BUFFER_FORMAT_YCbCr_420_SP,
	PLUGIN_BUFFER_FORMAT_YCrCb_420_SP,
	PLUGIN_BUFFER_FORMAT_NV12_ENCODEABLE,
	PLUGIN_BUFFER_FORMAT_YCbCr_420_SP_VENUS,
	PLUGIN_BUFFER_FORMAT_YCbCr_420_SP_VENUS_UBWC,
	PLUGIN_BUFFER_FORMAT_YCbCr_422_SP,
	PLUGIN_BUFFER_FORMAT_YCbCr_422_I,
	PLUGIN_BUFFER_FORMAT_INVALID = 0xFFFFFFFF,
};

/*****************CreateDisplay*****************/
/* Display type enum */
enum {
	PLUGIN_PRIMARY_DISPLAY = 0,
	PLUGIN_SECOND_DISPLAY,
	PLUGIN_TERTIARY_DISPLAY,
	PLUGIN_FOURTH_DISPLAY,
	PLUGIN_MAX_DISPLAYS,
};

#define PLUGIN_PRIMARY_DISPLAY_MASK 	(1 << PLUGIN_PRIMARY_DISPLAY)
#define PLUGIN_SECOND_DISPLAY_MASK	(1 << PLUGIN_SECOND_DISPLAY)
#define PLUGIN_TERTIARY_DISPLAY_MASK	(1 << PLUGIN_TERTIARY_DISPLAY)
#define PLUGIN_FOURTH_DISPLAY_MASK	(1 << PLUGIN_FOURTH_DISPLAY)
#define PLUGIN_DISPLAYS_MASK		(PLUGIN_PRIMARY_DISPLAY_MASK | \
					PLUGIN_SECOND_DISPLAY_MASK | \
					PLUGIN_TERTIARY_DISPLAY_MASK | \
					PLUGIN_FOURTH_DISPLAY_MASK)

/* Display attributes information. Similar to HWDisplayAttributes */
struct DisplayAttributes {
	bool is_device_split;
	uint32_t split_left;

	/* pixels in X-direction on the display panel */
	uint32_t x_pixels;
	/* pixels in Y-direction on the display panel */
	uint32_t y_pixels;
	/* Dots per inch in X-direction */
	float x_dpi;
	/* Dots per inch in Y-direction */
	float y_dpi;
	/* Frame rate per second */
	uint32_t fps;
	/* VSync period in nanoseconds */
	uint32_t vsync_period_ns;
	/* Vertical front porch of panel */
	uint32_t v_front_porch;
	/* Vertical front porch of panel */
	uint32_t v_back_porch;
	/* Vertical pulse width of panel */
	uint32_t v_pulse_width;
	/* Total width of panel (hActive + hFP + hBP + hPulseWidth) */
	uint32_t h_total;
};

/* Panel port information for PanelInfo.port */
enum {
	PLUGIN_PORT_DEFAULT,
	PLUGIN_PORT_DSI,
	PLUGIN_PORT_DTV,
	PLUGIN_PORT_WRITEBACK,
	PLUGIN_PORT_LVDS,
	PLUGIN_PORT_EDP,
};
/* Panel mode information for PanelInfo.mode */
enum {
	PLUGIN_MODE_DEFAULT,
	PLUGIN_MODE_VIDEO,
	PLUGIN_MODE_COMMAND,
};

/* Display panel information. Similar to HWPanelInfo */
struct PanelInfo {
	/* Display port */
	uint32_t port;
	/* Display mode */
	uint32_t mode;
	/* Partial update feature */
	bool partial_update;
	/* ROI left alignment restriction */
	int left_align;
	/* ROI width alignment restriction */
	int width_align;
	/* ROI top alignment restriction */
	int top_align;
	/* ROI height alignment restriction */
	int height_align;
	/* Min width needed for ROI */
	int min_roi_width;
	/* Min height needed for ROI */
	int min_roi_height;
	/* Merge ROI's of both the DSI's */
	bool needs_roi_merge;
	/* Panel Supports dynamic fps */
	bool dynamic_fps;
	/* Min fps supported by panel */
	uint32_t min_fps;
	/* Max fps supported by panel */
	uint32_t max_fps;
	/* Panel is primary display */
	bool is_primary_panel;
	/* Panel split configuration */
	uint32_t left_split;
	uint32_t right_split;
	/* Panel name */
	char panel_name[256];
};

/* Display parameter for CreateDisplay */
struct DisplayParameter {
	uint32_t type;
	struct DisplayAttributes attribs;
	struct PanelInfo panel;
	uint32_t max_blending_stages;
	uint32_t max_layers;
};

/*****************Property Relative*****************/
/* Bandwidth mode. Can be changed dynamically according to user scenario */
enum BwModes {
	PLUGIN_BW_DEFAULT = 0,
	PLUGIN_BW_CAMERA,
	PLUGIN_BW_VFLIP,
	PLUGIN_BW_HFLIP,
	PLUGIN_BW_MODE_MAX,
};

/* Limitiation for dynamic BW */
struct DynBwLimit {
	uint32_t cur_mode;
	uint32_t total_bw_limit[PLUGIN_BW_MODE_MAX];
	uint32_t pipe_bw_limit[PLUGIN_BW_MODE_MAX];
};

/* HW resource infomration. It is similar to HWResourceInfo in SDM module */
struct HWResourceConfig {
	uint32_t num_dma_pipe;
	uint32_t num_vig_pipe;
	uint32_t num_rgb_pipe;
	uint32_t num_cursor_pipe;
	uint32_t num_blending_stages;
	uint32_t num_rotator;
	uint32_t num_control;
	uint32_t num_mixer_to_disp;
	uint32_t max_scale_up;
	uint32_t max_scale_down;
	uint64_t max_bandwidth_low;
	uint64_t max_bandwidth_high;
	uint32_t max_mixer_width;
	uint32_t max_pipe_width;
	uint32_t max_cursor_size;
	uint32_t max_pipe_bw;
	uint32_t max_sde_clk;
	float clk_fudge_factor;
	uint32_t macrotile_nv12_factor;
	uint32_t macrotile_factor;
	uint32_t linear_factor;
	uint32_t scale_factor;
	uint32_t extra_fudge_factor;
	bool has_ubwc;
	bool has_decimation;
	bool has_rotator_downscale;
	bool has_non_scalar_rgb;
	bool is_src_split;
	bool perf_calc;
	bool has_dyn_bw_support;
	struct DynBwLimit dyn_bw_info;
};

/*****************Prepare*****************/
/* Composition type for each layer*/
enum {
	PLUGIN_COMPOSITION_GPU = 0,
	PLUGIN_COMPOSITION_OVERLAY,
	PLUGIN_COMPOSITION_HW_CURSOR,
	PLUGIN_COMPOSITION_FB_TARGET,
};

/* Blending type for each layer, now only support premultiplied blending */
enum {
	PLUGIN_BLENDING_NONE = 0,
	PLUGIN_BLENDING_PREMULTIPLIED,
};

/* Rotation type*/
enum {
	PLUGIN_TRANSFORM_NORMAL = 0x00,
	PLUGIN_TRANSFORM_FLIP_H = 0x01,
	PLUGIN_TRANSFORM_FLIP_V = 0x02,
	PLUGIN_TRANSFORM_90 = 0x04,
	PLUGIN_TRANSFORM_180 = 0x03,
	PLUGIN_TRANSFORM_270 = 0x07,
};

struct Rect {
	float left;
	float top;
	float right;
	float bottom;
};

struct  RectArray {
	struct Rect *rects;
	uint32_t count;
};

/*
 * Layer flag which only be configurated by compositor. It will affect the SDM
 * strategy result.
 */
struct LayerFlags {
	uint32_t skip : 1;
	uint32_t is_cursor : 1;
	uint32_t has_ubwc_buf : 1;
};

/*
 * Pipe geometry information filled by SDM. Compositor will update the
 * corresponding HW pipe according to it.
 */
struct sdmPipeInfo {
	uint32_t pipe_id;
	struct Rect src_roi;
	struct Rect dst_roi;
	uint8_t h_decimation;
	uint8_t v_decimation;
	uint32_t z_order;
	bool valid;
};

/*
 * Pipe information for a layer. One layer will occupy two hw pipes at most
 * (eg. 4K display). If a layer is composited by GPU, none pipe will be
 * assigned for it.
 */
struct sdmLayerConfig {
	struct sdmPipeInfo left_pipe;
	struct sdmPipeInfo right_pipe;
};

/* Layer geometry information filled by compositor */
struct LayerGeometry {
	/* Buffer information */
	uint32_t 		width;
	uint32_t 		height;
	uint32_t 		format;
	/* Layer information */
	uint32_t 		composition; /*GPU, Overlay, HWCursor*/
	struct Rect 		src_rect; /* srouce rectangle */
	struct Rect 		dst_rect; /* destination rectangle */
	struct RectArray 	visible_regions;
	struct RectArray 	dirty_regions;
	uint32_t 		blending;
	uint32_t 		transform;
	uint8_t 		plane_alpha; /* global alpha */
	struct LayerFlags 	flags;

	/*Hook for storing information relative to compositor. DO NOT MODIFY IT!!!*/
	const void *usr_data;
};

/* The configuration of all layers. Filled by compositor */
struct LayersConfig {
	struct LayerGeometry *layers;
	uint32_t count;
	/* Reserve for user setting. Refer to sdm LayerStackFlags */
	uint32_t flags;
	struct sdmLayerConfig sdm_layer_configs[MAX_SDE_Layers];
};

/*****************Property*****************/
/* Opcode for UpdateProperty, UpdateBlobProperty, ValidateProperty, QueryProperty */
enum {
	/* For update operation */
	PLUGIN_UPDATE_DISPLAY_ATTRIBUTES = 0,
	PLUGIN_UPDATE_PANEL_INFO,
	PLUGIN_UPDATE_IDLE_TIME,
	PLUGIN_UPDATE_HW_LAYERS,
	PLUGIN_UPDATE_THERMAL_LEVEL,
	PLUGIN_UPDATE_MAX_MIXER_STAGES,
	PLUGIN_UPDATE_PARTIAL_UPDATE,
	PLUGIN_UPDATE_BW_MODE,
	PLUGIN_UPDATE_HW_RESOURCE,
	PLUGIN_PURGE_DISPLAY_RESOURCE,
	/* For validation operation */
	PLUGIN_VALIDATE_SCALING,
	PLUGIN_VALIDATE_CURSOR_POSITION,
	/* For query operation */
	PLUGIN_QUERY_CAN_SET_IDLE_TIME,
	PLUGIN_MAX_PROPERTIES,
};

/* Display property */
#define PLUGIN_UPDATE_DISPLAY_ATTRIBUTES_MASK		(1 << PLUGIN_UPDATE_DISPLAY_ATTRIBUTES)
#define PLUGIN_UPDATE_PANEL_INFO_MASK 			(1 << PLUGIN_UPDATE_PANEL_INFO)
#define PLUGIN_UPDATE_IDLE_TIME_MASK 			(1 << PLUGIN_UPDATE_IDLE_TIME)
#define PLUGIN_UPDATE_HW_LAYERS_MASK 			(1 << PLUGIN_UPDATE_HW_LAYERS)
#define PLUGIN_UPDATE_THERMAL_LEVEL_MASK 		(1 << PLUGIN_UPDATE_THERMAL_LEVEL)
#define PLUGIN_UPDATE_MAX_MIXER_STAGES_MASK 		(1 << PLUGIN_UPDATE_MAX_MIXER_STAGES)
#define PLUGIN_UPDATE_PARTIAL_UPDATE_MASK 		(1 << PLUGIN_UPDATE_PARTIAL_UPDATE)
#define PLUGIN_PURGE_DISPLAY_RESOURCE_MASK 		(1 << PLUGIN_PURGE_DISPLAY_RESOURCE)
#define PLUGIN_VALIDATE_SCALING_MASK 			(1 << PLUGIN_VALIDATE_SCALING)
#define PLUGIN_VALIDATE_CURSOR_POSITION_MASK 		(1 << PLUGIN_VALIDATE_CURSOR_POSITION)
#define PLUGIN_QUERY_CAN_SET_IDLE_TIME_MASK 		(1 << PLUGIN_QUERY_CAN_SET_IDLE_TIME)
/* Global property */
#define PLUGIN_UPDATE_HW_RESOURCE_MASK 			(1 << PLUGIN_UPDATE_HW_RESOURCE)
#define PLUGIN_UPDATE_BW_MODE_MASK 			(1 << PLUGIN_UPDATE_BW_MODE)

#define PLUGIN_PROPERTIES_MASK (PLUGIN_UPDATE_DISPLAY_ATTRIBUTES_MASK | \
			PLUGIN_UPDATE_PANEL_INFO_MASK | \
			PLUGIN_UPDATE_IDLE_TIME_MASK | \
			PLUGIN_UPDATE_HW_LAYERS_MASK | \
			PLUGIN_UPDATE_THERMAL_LEVEL_MASK | \
			PLUGIN_UPDATE_MAX_MIXER_STAGES_MASK | \
			PLUGIN_UPDATE_PARTIAL_UPDATE_MASK | \
			PLUGIN_UPDATE_BW_MODE_MASK | \
			PLUGIN_UPDATE_HW_RESOURCE_MASK | \
			PLUGIN_PURGE_DISPLAY_RESOURCE_MASK | \
			PLUGIN_VALIDATE_SCALING_MASK | \
			PLUGIN_VALIDATE_CURSOR_POSITION_MASK | \
			PLUGIN_QUERY_CAN_SET_IDLE_TIME_MASK)

/* Scaling property */
struct ScalingInfo {
	struct Rect src;
	struct Rect dst;
	bool rotate_90;
};

/* Cursor position property */
struct CursorPosition {
	int x;
	int y;
};

/* Display property set by compositor */
struct DisplayProp {
	/*
	 * Display id. Now equal to display type. For some properties such as updating
	 * idle time and purging display resource, we only need to set display id
	 */
	uint32_t id;
	union {
		struct CursorPosition cur_pos;
		bool partial_update_enable;
		uint32_t max_mixer_stages;
		uint32_t thermal_level;
		bool is_idle; /* Used by PLUGIN_QUERY_CAN_SET_IDLE_TIME */
		struct PanelInfo panel;
		struct DisplayAttributes attribs;
		struct LayersConfig layer_configs;
	};
};

/* Property data set by compositor. Only one propery can be set or queried one time */
typedef union {
	struct DisplayProp disp_prop;
	struct  HWResourceConfig hw_res;
	struct ScalingInfo scaling;
	uint32_t max_bw_mode;
} PropertyData;

/* Input parameter for UpdateProperty, UpdateBlobProperty, ValidateProperty, QueryProperty */
struct Property {
	uint32_t opcode;
	PropertyData data;
};

/*****************Client Context*****************/
#define MAX_STRATEGY_CLIENTS 4

/* Client configuration. Reserve it for future use */
struct ClientConfigs {
};

/*****************Init*****************/
/* Compositor capability */
enum {
	/*
	 * Some compositors such as Weston don't know if the geometry of a
	 * surface is changed or not between last frame and current frame.
	 * So if compositor can't judge it, set the flag and then this plugin
	 * will do it internally. Now this feature has not been implemented
	 * yet.
	 */
	GEOMETRY_CHANGE = 0x00000001,
};

/* Compositor configuration. Set by compositor and used for Init function */
struct StrategyPluginConfig {
	/* A bit-wise mask of compositor capabilities */
	uint32_t compositor_caps;

	/* HW resource information */
	struct HWResourceConfig hw_res_config;
};


/*****************API*****************/
typedef void * plugin_handle_t;
typedef void * client_handle_t;
typedef void * display_handle_t;

/* API interface structure used by compositor */
struct StrategyPluginInterface {
	/*
	 * Init the plugin.
	 * @config: plugin configuration
	 * @flags: user can set special flag for this client, but it's not implemented now
	 */
	plugin_handle_t (*Init)(struct StrategyPluginConfig *config,
				uint32_t flags);

	/*
	 * Create a client and return the client handle to compositor.
	 * Display resource must be managed by a client.
	 * @configs: client configuration
	 * @flags: user can set special flag for this client, but it's not implemented now
	 */
	client_handle_t (*CreateClient)(struct ClientConfigs *configs,
					uint32_t flags);

	/*
	 * Destroy the client resources. Corresponding display resources will also be destroyed
	 * @handle: client handle
	 */
	int (*DestroyClient) (client_handle_t handle);

	/*
	 * Create a display for a client. Called after ClientClient.
	 * @handle: client handle
	 * @para: display parameter
	 * @flags: user can set special flag for this client, but it's not implemented now
	 */
	display_handle_t (*CreateDisplay)(client_handle_t handle,
					  struct DisplayParameter *para,
					  uint32_t flags);

	/*
	 * Destroy a display.
	 * @handle: client handle
	 * @display: display handle
	 */
	int (*DestroyDisplay) (client_handle_t handle,
			       display_handle_t display);

	/*
	 * Call SDM to filter strategy for a list of layer specified by display.
	 * Compositor will do corresponding operation for each layer later. It's
	 * allowed to update multiple displays of a client together.
	 * @handle: client handle
	 * @displays: display handle array
	 * @count: display count
	 * @configs: display configuration array
	 * @flags: user can set special flag for this client, but it's not implemented now
	 */
	int (*Prepare)(client_handle_t handle,
		       display_handle_t *displays,
		       int count,
		       struct LayersConfig **configs,
		       uint32_t flags);

	/*
	 * Commit this result to specified display. It's allowed to update multiple
	 * displays of a client together.
	 * @handle: client handle
	 * @displays: display handle array
	 * @count: display count
	 * @flags: user can set special flag for this client, but it's not implemented now
	 */
	int (*Commit)(client_handle_t handle,
		      display_handle_t *displays,
		      int count,
		      uint32_t flags);

	/*
	 * Update the property for a client.
	 * @handle: client handle
	 * @prop: property to be updated for this client
	 * @flush: apply the change of property immediately or not. If it's false, the
	 *         property will be cached until ApplyProperty is called.
	 * @flags: user can set special flag for this client, but it's not implemented now
	 */
	int (*UpdateProperty)(client_handle_t handle,
			      struct Property *prop,
			      bool flush,
			      uint32_t flags);

	/*
	 * Query the property for a client.
	 * @handle: client handle
	 * @prop: return property data for this query operation
	 * @flags: user can set special flag for this client, but it's not implemented now
	 */
	int (*QueryProperty)(client_handle_t handle,
			     struct Property *prop,
			     uint32_t flags);

	/*
	 * Check if the property can by supported by SDM.
	 * @handle: client handle
	 * @prop: property to be validated
	 * @flags: user can set special flag for this client, but it's not implemented now
	 */
	int (*ValidateProperty)(client_handle_t handle,
				struct Property *prop,
				uint32_t flags);

	/*
	 * Flush all cached properties which were updated before.
	 * @handle: client handle
	 * @flags: user can set special flag for this client, but it's not implemented now
	 */
	int (*ApplyProperty)(client_handle_t handle,
			     uint32_t flags);

	/*
	 * Deinit the plugin. Release all resources created by the plugin including client,
	 * display and so on.
	 * @handle: the plugin handle returned by Init function
	 */
	int (*Deinit)(plugin_handle_t handle);
};

#endif /* __SDM_STRATEGY_PLUGIN_INTERFACE_H__ */
