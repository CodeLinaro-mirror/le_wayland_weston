/*
* Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
*
* Copyright © 2008-2011 Kristian Hørg
* Copyright © 2011 Intel Corporation
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice (including the
* next paragraph) shall be included in all copies or substantial
* portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
* BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
* ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
* CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/

#include "drm_display.h"
#include "drm_interface.h"
#include "drm_master.h"
#include "drm_lib_loader.h"
#include <string.h>
#include <error.h>
#include <dlfcn.h>
#include <ctype.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <vector>
#include "compositor.h"
#include "../sdm-service/compositor-sdm-output.h"


using drm_utils::DRMMaster;
using drm_utils::DRMLibLoader;
using sde_drm::DRMDisplayType;
using sde_drm::DRMPlaneType;
using sde_drm::DRMOps;
using sde_drm::DRMRect;
using drm_utils::DRMBuffer;
using sde_drm::DRMAtomicReqInterface;


#ifdef __cplusplus
extern "C" {
#endif

enum display_id {
	DISPFirst,
	DISPSecondary,
	DISPTertiary,
	DISPMax
};

struct early_plane {
	uint32_t pipe_id; /* hardware pipe id */
	uint32_t output_mask; /* bitmask of possible output */
	bool is_yuv; /* whether the hw pipe supports YUV format */
	display_id cur_display_id; /* the assigned display id in this round */
	display_id pre_display_id; /* the assigned display id in last round */
};

struct Rect {
       float left;
       float top;
       float right;
       float bottom;
};

sde_drm::DRMManagerInterface *drm_mgr_intf_ = {};
std::vector<sde_drm::DRMDisplayToken> token_list;
std::vector<struct early_plane> plane_list;
std::vector<DRMAtomicReqInterface *> drm_atomic_intfs;
std::vector<uint32_t> max_blend_stages_list;

int early_get_drm_master() {
	DRMMaster *master = nullptr;
	DRMMaster::GetInstance(&master);

	if(!master) {
		weston_log("Failed to get DRMMaster instance\n");
		return -1;
	}

	int fd;
	master->GetHandle(&fd);
	master->UseExternalGemHandle();
	return fd;
}

int early_drm_get_planes() {
	sde_drm::DRMPlanesInfo planes;
	uint32_t plane_num = 0;
	uint32_t primary_cnt = 0;

	if (!drm_mgr_intf_)
		return -1;

	drm_mgr_intf_->GetPlanesInfo(&planes);
	for (auto &pipe_obj : planes) {
		struct early_plane early_plane = {};

		/* only use YUV, RGB, DMA pipe for early display */
		if (pipe_obj.second.type == DRMPlaneType::CURSOR)
			continue;

		early_plane.pipe_id = pipe_obj.first;
		early_plane.output_mask = pipe_obj.second.hw_block_mask;
		early_plane.cur_display_id = DISPMax;
		early_plane.pre_display_id = DISPMax;
		if (pipe_obj.second.type == DRMPlaneType::VIG)
			early_plane.is_yuv = true;
		plane_list.push_back(early_plane);
		plane_num++;
	}

	if (!plane_num) {
		weston_log("none available pipes detected\n");
		return -1;
	}

	weston_log("%d pipes detected\n", plane_num);
	return 0;
}

int early_drm_display_init(int drm_fd) {
	int ret = -1;

	if(!DRMLibLoader::GetInstance()->IsLoaded()) {
		weston_log("drm lib load failed\n");
		return -1;
	}

	DRMLibLoader::GetInstance()->FuncGetDRMManager()(drm_fd, &drm_mgr_intf_);
	if (!drm_mgr_intf_)
		return -1;

	ret = early_drm_get_planes();
	if (ret) {
		weston_log("drm get planes failed\n");
		return ret;
	}

	return 0;
}

int early_get_connector_count(uint32_t *count) {
	if (!drm_mgr_intf_)
		return -1;

	*count = drm_mgr_intf_->GetConnectorCount();
	return 0;
}

int early_create_display(uint32_t order, struct EarlyDisplayInfo *dispinfo) {
	sde_drm::DRMConnectorInfo info;
	sde_drm::DRMDisplayType disp_type;
	char name[32];
	const char *type_name = NULL;
	drmModeModeInfo current_mode_ = {};
	sde_drm::DRMDisplayToken token_ = {};
	DRMAtomicReqInterface *drm_atomic_intf_;
	sde_drm::DRMCrtcInfo crtc_info;
	uint32_t max_blend_stages;
	int ret = -1;

	if (!drm_mgr_intf_)
		return -1;

	drm_mgr_intf_->GetConnectorInfoByOrder(sde_drm::DRMDisplayOrder(order), &info);
	current_mode_ = info.modes[0];

	if(info.type == DRM_MODE_CONNECTOR_HDMIA) {
		disp_type = DRMDisplayType::TV;
		type_name = "HDMI-A";
	}
	else if(info.type == DRM_MODE_CONNECTOR_DSI) {
		disp_type = DRMDisplayType::PERIPHERAL;
		type_name = "DSI";
	}
	else {
		weston_log("unknown connector type.\n");
		return -1;
	}
	snprintf(name, sizeof name, "%s-%d", type_name, info.type_id);

	dispinfo->name = strdup(name);
	dispinfo->x_pixels = current_mode_.hdisplay;
	dispinfo->y_pixels = current_mode_.vdisplay;
	dispinfo->fps = current_mode_.vrefresh;

	/*
	 * Need to register display even if early display is not enabled
	 * for the output to make it consistent with sdm
	 */
	ret = drm_mgr_intf_->RegisterDisplay(info.display_order, disp_type, &token_);
	if (ret) {
		weston_log("RegisterDisplay failed");
		dispinfo->early_enable = false;
		return -1;
	}
	dispinfo->crtc_id = token_.crtc_id;
	dispinfo->conn_id = token_.conn_id;
	token_list.push_back(token_);
	weston_log("%s registered, reserved CRTC %d, reserved Connector %d\n",
		name, token_.crtc_id, token_.conn_id);

	drm_mgr_intf_->CreateAtomicReq(token_, &drm_atomic_intf_);
	drm_atomic_intfs.push_back(drm_atomic_intf_);

	drm_mgr_intf_->GetCrtcInfo(token_.crtc_id, &crtc_info);
	max_blend_stages_list.push_back(crtc_info.max_blend_stages);

	if(!dispinfo->early_enable)
		return 0;

	drm_atomic_intf_->Perform(DRMOps::CRTC_SET_MODE, token_.crtc_id, &current_mode_);
	drm_atomic_intf_->Perform(DRMOps::CRTC_SET_ACTIVE, token_.crtc_id, 1);
	drm_atomic_intf_->Perform(DRMOps::CRTC_SET_OUTPUT_FENCE_OFFSET, token_.crtc_id, 1);
	drm_atomic_intf_->Perform(DRMOps::CONNECTOR_SET_POWER_MODE,
		token_.conn_id, sde_drm::DRMPowerMode::ON);
	ret = drm_atomic_intf_->Commit(false /* asynchronous */, NULL);
	if (ret) {
		weston_log("Set power mode on failed for connector %d\n", token_.conn_id);
		dispinfo->early_enable = false;
		return 0;
	}
	weston_log("Set power mode on for connector %d\n", token_.conn_id);
	weston_place_marker("W - connector power on");

	return 0;
}

static bool is_yuv_format(uint32_t fmt) {
	bool is_yuv;

	switch (fmt) {
		case GBM_FORMAT_RGB565:
		case GBM_FORMAT_BGR565:
		case GBM_FORMAT_RGB888:
		case GBM_FORMAT_RGBA8888:
		case GBM_FORMAT_BGRA8888:
		case GBM_FORMAT_RGBX8888:
		case GBM_FORMAT_XRGB8888:
		case GBM_FORMAT_XBGR8888:
		case GBM_FORMAT_ARGB8888:
		case GBM_FORMAT_ABGR8888:
		case GBM_FORMAT_ABGR2101010:
			is_yuv = false;
			break;
		default:
			is_yuv = true;
			break;
	}

	return is_yuv;
}

static int early_get_drm_fb_id(int drm_fd, struct gbm_bo *bo, uint32_t *fb_id)
{
	uint32_t width, height;
	uint32_t handles[4], pitches[4], offsets[4];
	uint32_t stride, handle;
	uint32_t format;
	int ion_fd;
	int ret = -1;
	generic_buf_layout_t buf_layout;

	width = gbm_bo_get_width(bo);
	height = gbm_bo_get_height(bo);
	handle = gbm_bo_get_handle(bo).u32;
	format = gbm_bo_get_format(bo);

	ret = gbm_perform(GBM_PERFORM_GET_PLANE_INFO, bo, &buf_layout);
	if (ret == GBM_ERROR_NONE) {
		uint32_t num_planes = buf_layout.num_planes;
		 for(int j = 0; j < num_planes; j++) {
			handles[j] = handle;
			offsets[j] = buf_layout.planes[j].offset;
			pitches[j] = buf_layout.planes[j].v_increment;
		}
	} else {
		weston_log("Get Plane info failed\n");
		return ret;
	}

	uint32_t alignedHeight = 0;
	ret = gbm_perform(GBM_PERFORM_GET_BO_ALIGNED_HEIGHT, bo, &alignedHeight);
	if (ret != GBM_ERROR_NONE) {
		weston_log("Get aligned height failed\n");
		return ret;
	}

	/*
	* This is special for NV12 ubwc format, offset[0]
	* is not 0 which get from gbm if the buffer have
	* ubwc flag
	*/
	if (format == GBM_FORMAT_NV12) {
		pitches[0] = buf_layout.planes[0].v_increment;
		offsets[0] = 0;
		pitches[1] = pitches[0];
		offsets[1] = pitches[0] * alignedHeight;
	}

	ret = drmModeAddFB2(drm_fd, width, height,
					  format, handles, pitches, offsets,
					  fb_id, 0);
	if (ret) {
		weston_log("addfb2 failed: %m\n");
		return ret;
	}

	return 0;
}

static void early_layer_destroy_callback(struct gbm_bo *bo,
		void *data)
{
	struct early_layer* layer = (struct early_layer*) data;
	struct gbm_device *gbm = gbm_bo_get_device(bo);

	if (layer->fb_id)
		drmModeRmFB(gbm_device_get_fd(gbm), layer->fb_id);

}

int early_layer_prepare(struct early_layer *layer, struct drm_output *output) {
	struct weston_view *ev = layer->view;
	struct weston_buffer *buffer = ev->surface->buffer_ref.buffer;
	struct gbm_bo *bo;
	struct gbm_buffer *gbm_buf;
	uint32_t fb_id;
	struct drm_backend *b =
		(struct drm_backend *)output->base.compositor->backend;
	int ret = -1;

	assert(buffer != NULL);

	if (!(gbm_buf = gbm_buffer_get(buffer->resource))) {
		weston_log("only gbm buffer is supported for early display\n");
		return ret;
	}

	struct gbm_buf_info gbm_bufinfo = {
		.fd = gbm_buf->fd,
		.metadata_fd = gbm_buf->metadata_fd,
		.width = gbm_buf->width,
		.height = gbm_buf->height,
		.format = gbm_buf->format
	};

	bo = gbm_bo_import(b->gbm, GBM_BO_IMPORT_GBM_BUF_TYPE,
		&gbm_bufinfo, GBM_BO_USE_SCANOUT);

	if (bo == NULL) {
		weston_log("gbm bo import failed\n");
		return ret;
	}

	if (early_get_drm_fb_id(b->drm.fd, bo, &fb_id)) {
		gbm_bo_destroy(bo);
		return ret;
	}

	layer->fb_id = fb_id;
	layer->bo = bo;
	layer->yuv_required = is_yuv_format(gbm_buf->format);
	weston_buffer_reference(&layer->buffer_ref, buffer);
	gbm_bo_set_user_data(bo, layer, early_layer_destroy_callback);

	return 0;
}

static uint32_t early_search_pipe(display_id disp_id, bool yuv_required) {
	std::vector<struct early_plane>::iterator itr = plane_list.begin();
	std::vector<struct early_plane>::iterator itr_backup = plane_list.end();

	for (; itr != plane_list.end(); itr++) {
		if (!(itr->output_mask & (1 << disp_id)))
			continue;
		/*
		* there is a hardware limitation that a pipe should not be
		* assigned to different crtcs in two contiguous commits,
		* so, search a free pipe or a pipe assigned to the same output
		* in last round.
		*/
		if ( (itr->pre_display_id == DISPMax ||
				itr->pre_display_id == disp_id ) &&
				itr->cur_display_id == DISPMax) {
			/* YUV format needs VIG pipe */
			if (yuv_required) {
				if (itr->is_yuv) {
					itr->cur_display_id = disp_id;
					return itr->pipe_id;
				} else
					continue;
				} else {
				/*
				* For RGB format, all DMA/RGB/VIG pipes could
				* be used, but should not occupy VIG pipe uless
				* there is no free DMA/RGB pipes
				*/
				if (!itr->is_yuv) {
					itr->cur_display_id = disp_id;
					return itr->pipe_id;
				} else /* Back up a alternative vig pipe */
					itr_backup = itr;
			}
		}
	}

	if (itr_backup != plane_list.end()) {
		itr_backup->cur_display_id = disp_id;
		return itr_backup->pipe_id;
	}

	return 0;
}

static void early_compute_src_dst_rect(struct drm_output *output, struct weston_view *ev,
	struct Rect *src_ret, struct Rect *dst_ret)
{
	struct weston_buffer_viewport *viewport = &ev->surface->buffer_viewport;
	pixman_region32_t src_rect, dest_rect;
	pixman_box32_t *box, tbox;
	wl_fixed_t sx1, sy1, sx2, sy2;

	/* dst rect */
	pixman_region32_init(&dest_rect);
	pixman_region32_intersect(&dest_rect, &ev->transform.boundingbox, &output->base.region);

	pixman_region32_translate(&dest_rect, -output->base.x, -output->base.y);
	box = pixman_region32_extents(&dest_rect);

	{
		enum wl_output_transform buffer_transform1 = WL_OUTPUT_TRANSFORM_NORMAL;

		switch(output->base.transform) {
			case 0:
				buffer_transform1 = WL_OUTPUT_TRANSFORM_NORMAL;
				break;
			case 1:
				buffer_transform1 = WL_OUTPUT_TRANSFORM_90;
				break;
			case 2:
				buffer_transform1 = WL_OUTPUT_TRANSFORM_180;
				break;
			case 3:
				buffer_transform1 = WL_OUTPUT_TRANSFORM_270;
				break;
			case 4:
				buffer_transform1 = WL_OUTPUT_TRANSFORM_FLIPPED;
				break;
			case 5:
				buffer_transform1 = WL_OUTPUT_TRANSFORM_FLIPPED_90;
				break;
			case 6:
				buffer_transform1 = WL_OUTPUT_TRANSFORM_FLIPPED_180;
				break;
			case 7:
				buffer_transform1 = WL_OUTPUT_TRANSFORM_FLIPPED_270;
				break;
			default:
				weston_log("Invalid buffer transform not supported: %d",
						output->base.transform);
				pixman_region32_fini(&dest_rect);
				return;
		}

		tbox = weston_transformed_rect(output->base.width,
				output->base.height,
				buffer_transform1,
				output->base.current_scale,
				*box);
	}

	dst_ret->left = (float)tbox.x1;
	dst_ret->top = (float)tbox.y1;
	dst_ret->right = (float)tbox.x2;
	dst_ret->bottom = (float)tbox.y2;
	pixman_region32_fini(&dest_rect);

	/* src rect */
	pixman_region32_init(&src_rect);
	pixman_region32_intersect(&src_rect, &ev->transform.boundingbox,
			&output->base.region);
	box = pixman_region32_extents(&src_rect);

	weston_view_from_global_fixed(ev,
			wl_fixed_from_int(box->x1),
			wl_fixed_from_int(box->y1),
			&sx1, &sy1);
	weston_view_from_global_fixed(ev,
			wl_fixed_from_int(box->x2),
			wl_fixed_from_int(box->y2),
			&sx2, &sy2);

	if (sx1 < 0)
		sx1 = 0;
	if (sy1 < 0)
		sy1 = 0;
	if (sx2 > wl_fixed_from_int(ev->surface->width))
		sx2 = wl_fixed_from_int(ev->surface->width);
	if (sy2 > wl_fixed_from_int(ev->surface->height))
		sy2 = wl_fixed_from_int(ev->surface->height);

	tbox.x1 = sx1;
	tbox.y1 = sy1;
	tbox.x2 = sx2;
	tbox.y2 = sy2;

	{
		enum wl_output_transform buffer_transform2 =
			WL_OUTPUT_TRANSFORM_NORMAL;

		switch(viewport->buffer.transform) {
		case 0:
			buffer_transform2 = WL_OUTPUT_TRANSFORM_NORMAL;
			break;
		case 1:
			buffer_transform2 = WL_OUTPUT_TRANSFORM_90;
			break;
		case 2:
			buffer_transform2 = WL_OUTPUT_TRANSFORM_180;
			break;
		case 3:
			buffer_transform2 = WL_OUTPUT_TRANSFORM_270;
			break;
		case 4:
			buffer_transform2 = WL_OUTPUT_TRANSFORM_FLIPPED;
			break;
		case 5:
			buffer_transform2 = WL_OUTPUT_TRANSFORM_FLIPPED_90;
			break;
		case 6:
			buffer_transform2 = WL_OUTPUT_TRANSFORM_FLIPPED_180;
			break;
		case 7:
			buffer_transform2 = WL_OUTPUT_TRANSFORM_FLIPPED_270;
			break;
		default:
			weston_log("Invalid buffer transform not supported: %d",
					viewport->buffer.transform);
			pixman_region32_fini(&src_rect);
			return;
		}

		tbox = weston_transformed_rect(wl_fixed_from_int(ev->surface->width),
					wl_fixed_from_int(ev->surface->height),
					buffer_transform2,
					viewport->buffer.scale,
					tbox);
	}

	src_ret->left = (float)(tbox.x1 >> 8);
	src_ret->top = (float)(tbox.y1 >> 8);
	src_ret->right = (float)(tbox.x2 >> 8);
	src_ret->bottom = (float)(tbox.y2 >> 8);
	pixman_region32_fini(&src_rect);
}

static void early_layer_setup_atomic(struct early_layer *layer,
		DRMAtomicReqInterface *drm_atomic_intf_, struct drm_output *output) {
	struct Rect src_rect, dst_rect;

	assert(drm_atomic_intf_ != NULL);

	drm_atomic_intf_->Perform(DRMOps::PLANE_SET_CRTC,
			layer->pipe_id, output->crtc_id);
	drm_atomic_intf_->Perform(DRMOps::PLANE_SET_FB_ID,
			layer->pipe_id, layer->fb_id);

	early_compute_src_dst_rect(output, layer->view, &src_rect, &dst_rect);
	DRMRect src = {};
	src.left = (int)src_rect.left;
	src.right = (int)src_rect.right;
	src.top = (int)src_rect.top;
	src.bottom = (int)src_rect.bottom;
	drm_atomic_intf_->Perform(DRMOps::PLANE_SET_SRC_RECT,
			layer->pipe_id, src);

	DRMRect dst = {};
	dst.left = (int)dst_rect.left;
	dst.right = (int)dst_rect.right;
	dst.top = (int)dst_rect.top;
	dst.bottom = (int)dst_rect.bottom;
	drm_atomic_intf_->Perform(DRMOps::PLANE_SET_DST_RECT,
			layer->pipe_id, dst);

	drm_atomic_intf_->Perform(DRMOps::PLANE_SET_ZORDER,
			layer->pipe_id, layer->z_order);
}

static void early_reset_planes(display_id disp_id) {
	std::vector<struct early_plane>::iterator itr = plane_list.begin();

	for (; itr != plane_list.end(); itr++) {
		/*
		* skip the pipe assigned to other displays
		*/
		if (itr->pre_display_id != disp_id &&
				itr->pre_display_id != DISPMax)
			continue;

		itr->pre_display_id = itr->cur_display_id;
		itr->cur_display_id = DISPMax;
	}
}

int early_prepare(struct drm_output *output) {
	struct early_layer *layer, *next_layer;
	uint32_t pipe_id;
	struct drm_backend *b =
		(struct drm_backend *)output->base.compositor->backend;
	DRMAtomicReqInterface *drm_atomic_intf_ =
				drm_atomic_intfs[output->display_id];
	uint32_t pipe_count = 0;
	uint32_t z_order = 0;
	uint32_t max_blend_stages = max_blend_stages_list[output->display_id];


	wl_list_for_each_safe(layer, next_layer, &output->early_layer_list, link) {
		layer->pipe_id = early_search_pipe(display_id(output->display_id),
					layer->yuv_required);
		if (layer->pipe_id) {
			/*
			* Early display does not support GPU composition.
			* If early layer num is bigger than max_blend_stages,
			* the extra layers are skipped.
			*/
			if (z_order++ >= max_blend_stages)
				break;
			layer->z_order = z_order;
			early_layer_setup_atomic(layer, drm_atomic_intf_, output);
			pipe_count++;
		}
	}

	if (!pipe_count) {
		weston_log("early prepare failed\n");
		return -1;
	}

	return 0;
}


int early_commit(struct drm_output *output) {
	DRMAtomicReqInterface *drm_atomic_intf_ =
				drm_atomic_intfs[output->display_id];

	int ret = drm_atomic_intf_->Commit(false, NULL);
	if(ret) {
		weston_log("early commit failed\n");
		return ret;
	}

	early_reset_planes(display_id(output->display_id));

	int retire_fence = -1;
	drm_atomic_intf_->Perform(DRMOps::CONNECTOR_GET_RETIRE_FENCE,
		output->connector_id, &retire_fence);

	output->retire_fence_fd = retire_fence;

	return 0;
}

/* Unregister token in token_list in order not to block SDM register
 * display
 */
void early_unregister_displays() {
	if(!drm_mgr_intf_)
		return;

	std::vector<sde_drm::DRMDisplayToken>::iterator itr = token_list.begin();
	for(; itr != token_list.end(); itr++) {
		sde_drm::DRMDisplayToken token_ = *itr;
		drm_mgr_intf_->UnregisterDisplay(token_);
	}
}

void early_drm_destroy_displays() {
	std::vector<DRMAtomicReqInterface *>::iterator itr = drm_atomic_intfs.begin();

	for (; itr != drm_atomic_intfs.end(); ++itr) {
		DRMAtomicReqInterface *drm_atomic_intf_ =
				*itr;
		drm_mgr_intf_->DestroyAtomicReq(drm_atomic_intf_);
	}
	drm_atomic_intfs.clear();
	token_list.clear();
	plane_list.clear();
	max_blend_stages_list.clear();

}

#ifdef __cplusplus
}
#endif
