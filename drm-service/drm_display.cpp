/*
* Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
*
* Copyright © 2008-2011 Kristian Høgsberg
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
#include <drm/drm_fourcc.h>
#include <vector>
#ifdef __cplusplus
extern "C" {
#endif
#include "../sdm-service/compositor-sdm-output.h"
#ifdef __cplusplus
}
#endif


#ifndef DRM_FORMAT_MOD_QCOM_COMPRESSED
#define DRM_FORMAT_MOD_QCOM_COMPRESSED fourcc_mod_code(QCOM, 1)
#endif

using sde_drm::DRMManagerInterface;
using drm_utils::DRMMaster;
using drm_utils::DRMLibLoader;
using sde_drm::DRMDisplayType;
using sde_drm::DRMPlaneType;
using sde_drm::DRMOps;
using sde_drm::DRMRect;
using drm_utils::DRMBuffer;
using sde_drm::DRMAtomicReqInterface;
using sde_drm::DRMDisplayToken;


#ifdef __cplusplus
extern "C" {
#endif

enum display_id {
  DISPFirst,
  DISPSecondary,
  DISPTertiary,
  DISPQuaternary,
  DISPMax
};

struct early_plane {
  uint32_t pipe_id; /* hardware pipe id */
  //uint32_t output_mask; /* bitmask of possible output */
  bool is_yuv; /* whether the hw pipe supports YUV format */
  bool block_sec_ui = false;
  bool is_virtual = false; /* True if pipe is virtual pipe in smart sma */
  /* index of possible crtcs, allow all pipelines to be usable on all displays by default */
  std::bitset<32> hw_block_mask = std::bitset<32>().set();
  display_id cur_display_id; /* the assigned display id in this round */
  display_id pre_display_id; /* the assigned display id in last round */
};

struct Rect {
  float left;
  float top;
  float right;
  float bottom;
};

static sde_drm::DRMManagerInterface *drm_mgr_intf_ = {};
static std::vector<struct early_plane> plane_list;
static std::map<uint32_t, uint32_t> display_id_connid = {};

struct early_display {
  uint32_t max_blend_stages;
  DRMAtomicReqInterface *drm_atomic_intf_;
  /* this token_ could be freed early in unRegisterDisplay*/
  DRMDisplayToken token_;
  int32_t hw_block_id;
  int32_t connector_id;
  int32_t crtc_id;
};

int early_get_drm_master() {
  DRMMaster *master = nullptr;
  DRMMaster::GetInstance(&master);

  if(!master) {
    weston_log("Failed to get DRMMaster instance\n");
    return -1;
  }

  int fd;
  master->GetHandle(&fd);
  return fd;
}

int early_drm_get_planes() {
  sde_drm::DRMPlanesInfo planes;
  uint32_t plane_num = 0;

  if (!drm_mgr_intf_)
    return -1;

  drm_mgr_intf_->GetPlanesInfo(&planes);
  for (auto &pipe_obj : planes) {
    struct early_plane early_plane = {};

    /* only use YUV, RGB, DMA pipe for early display */
    if (pipe_obj.second.type == DRMPlaneType::CURSOR)
      continue;
    uint32_t master_plane_id = pipe_obj.second.master_plane_id;
    if (master_plane_id)
      early_plane.is_virtual = true;
    early_plane.pipe_id = pipe_obj.first;
    early_plane.block_sec_ui = pipe_obj.second.block_sec_ui;
    early_plane.hw_block_mask = pipe_obj.second.hw_block_mask;
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

  DRMLibLoader *drm_lib_loader = DRMLibLoader::GetInstance();
  if (drm_lib_loader == NULL) {
    weston_log("drmlib instance fail\n");
    return -1;
  }

  if (!drm_lib_loader->IsLoaded()) {
    weston_log("drm lib load failed\n");
    return -1;
  }

  if (drm_lib_loader->FuncGetDRMManager()) {
    drm_lib_loader->FuncGetDRMManager()(drm_fd, &drm_mgr_intf_);
    if (!drm_mgr_intf_)
      return -1;
  }

  ret = early_drm_get_planes();
  if (ret) {
    weston_log("drm get planes failed\n");
    return ret;
  }

  return 0;
}

uint32_t early_get_connector_id(uint32_t display_id) {
  if (display_id >= DISPMax)
    return -1;
  return display_id_connid[display_id];
}

int early_get_connector_count(uint32_t *count) {
  int i = 0;
  int32_t conn_id;
  bool is_connected;

  if (!drm_mgr_intf_)
    return -1;

  sde_drm::DRMConnectorsInfo conns_info = {};
  int err = drm_mgr_intf_->GetConnectorsInfo(&conns_info);
  if (err) {
    weston_log("DRM Driver error %d while getting connectors' status!", err);
    return -1;
  }

  /* primary display*/
  for (auto &iter : conns_info) {
    conn_id = ((0 == iter.first) || (iter.first > INT32_MAX)) ? -1 : (int32_t)(iter.first);
    if (conn_id < 0)
      continue;
    if (!iter.second.is_primary)
      continue;
    is_connected = iter.second.is_connected ? 1 : 0;
    if (!is_connected)
      continue;
    display_id_connid[i] = iter.first;
    i++;
    /* only have one primary display*/
    break;
  }

  /* builtIn display*/
  for (auto &iter : conns_info) {
    conn_id = ((0 == iter.first) || (iter.first > INT32_MAX)) ? -1 : (int32_t)(iter.first);
    if (conn_id < 0)
      continue;
    if (iter.second.is_primary)
      continue;
    is_connected = iter.second.is_connected ? 1 : 0;
    if (!is_connected)
      continue;
    switch (iter.second.type) {
      case DRM_MODE_CONNECTOR_DSI:
        display_id_connid[i] = iter.first;
        i++;
      default:
        continue;
    }
  }

  /* Pluggable display*/
  for (auto &iter : conns_info) {
    conn_id = ((0 == iter.first) || (iter.first > INT32_MAX)) ? -1 : (int32_t)(iter.first);
    if (conn_id < 0)
      continue;
    if (iter.second.is_primary)
      continue;
    is_connected = iter.second.is_connected ? 1 : 0;
    if (!is_connected)
      continue;
    switch (iter.second.type) {
      case DRM_MODE_CONNECTOR_TV:
      case DRM_MODE_CONNECTOR_HDMIA:
      case DRM_MODE_CONNECTOR_HDMIB:
      case DRM_MODE_CONNECTOR_DisplayPort:
      case DRM_MODE_CONNECTOR_VGA:
        display_id_connid[i] = iter.first;
        i++;
        break;
      default:
        continue;
    }
  }

  *count = i;

  return 0;
}

int early_create_display(uint32_t display_id, struct EarlyDisplayInfo *dispinfo) {
  sde_drm::DRMConnectorInfo info;
  sde_drm::DRMDisplayType disp_type;
  char name[32];
  const char *type_name = NULL;
  int32_t conn_id = -1;
  drmModeModeInfo current_mode_ = {};
  sde_drm::DRMDisplayToken token_ = {};
  DRMAtomicReqInterface *drm_atomic_intf_;
  sde_drm::DRMCrtcInfo crtc_info;
  struct early_display *early_disp;
  int64_t release_fence = -1;
  int ret = -1;

  if (!drm_mgr_intf_)
    return -1;

  conn_id = early_get_connector_id(display_id);
  if (conn_id < 0)
    return -1;

  drm_mgr_intf_->GetConnectorInfo(conn_id, &info);

  weston_log("[%s] num modes: %d\n", __func__, info.modes.size());
  if (info.modes.size() == 0) {
      weston_log("no mode found.\n");
      return -1;
  }
  current_mode_ = info.modes[0].mode;

  switch (info.type) {
    case DRM_MODE_CONNECTOR_DSI:
      disp_type = DRMDisplayType::PERIPHERAL;
      type_name = "DSI";
      break;
    case DRM_MODE_CONNECTOR_TV:
    case DRM_MODE_CONNECTOR_HDMIA:
    case DRM_MODE_CONNECTOR_HDMIB:
    case DRM_MODE_CONNECTOR_DisplayPort:
    case DRM_MODE_CONNECTOR_VGA:
      disp_type = DRMDisplayType::TV;
      type_name = "DP";
      break;
    default:
      weston_log("unknown connector type.\n");
      return -1;
  }

  snprintf(name, sizeof name, "%s-%d", type_name, info.type_id);

  dispinfo->name = strdup(name);
  dispinfo->x_pixels = current_mode_.hdisplay;
  dispinfo->y_pixels = current_mode_.vdisplay;
  dispinfo->fps = current_mode_.vrefresh;

  early_disp = (struct early_display*)zalloc(sizeof(*early_disp));
  if (!early_disp)
    return -1;

  dispinfo->early_diplay_intf = (void *)early_disp;

  /*
   * Need to register display even if early display is not enabled
   * for the output to make it consistent with sdm
   */
  ret = drm_mgr_intf_->RegisterDisplay(conn_id, &token_);
  if (ret) {
    weston_log("RegisterDisplay failed");
    dispinfo->early_enable = false;
    goto err_register;
  }

  early_disp->token_.crtc_id = token_.crtc_id;
  early_disp->token_.conn_id = token_.conn_id;
  early_disp->token_.encoder_id = token_.encoder_id;
  early_disp->hw_block_id = token_.crtc_index;
  early_disp->connector_id = token_.conn_id;
  early_disp->crtc_id = token_.crtc_id;
  weston_log("%s registered, reserved CRTC %d, reserved Connector %d\n",
    name, token_.crtc_id, token_.conn_id);

  if(!dispinfo->early_enable)
    return 0;

  drm_mgr_intf_->CreateAtomicReq(token_, &drm_atomic_intf_);
  early_disp->drm_atomic_intf_ = drm_atomic_intf_;

  drm_mgr_intf_->GetCrtcInfo(token_.crtc_id, &crtc_info);
  early_disp->max_blend_stages = crtc_info.max_blend_stages;

  drm_atomic_intf_->Perform(DRMOps::CRTC_SET_MODE, token_.crtc_id, &current_mode_);
  drm_atomic_intf_->Perform(DRMOps::CRTC_SET_ACTIVE, token_.crtc_id, 1);
  drm_atomic_intf_->Perform(DRMOps::CONNECTOR_SET_CRTC, token_.conn_id, token_.crtc_id);
  //drm_atomic_intf_->Perform(DRMOps::CRTC_SET_OUTPUT_FENCE_OFFSET, token_.crtc_id, 1);
  drm_atomic_intf_->Perform(DRMOps::CRTC_GET_RELEASE_FENCE, token_.crtc_id, &release_fence);
  drm_atomic_intf_->Perform(DRMOps::CONNECTOR_SET_POWER_MODE,
    token_.conn_id, 0 /*sde_drm::DRMPowerMode::ON*/);

  ret = drm_atomic_intf_->Commit(true /* synchronous */, true /* retain_planes */);
  if (ret) {
    weston_log("Set power mode on failed for connector %d\n", token_.conn_id);
    dispinfo->early_enable = false;
    goto err_commit;
  }
  weston_log("Set power mode on for connector %d\n", token_.conn_id);

  return 0;

err_commit:
  drm_mgr_intf_->UnregisterDisplay(&token_);
err_register:
  free(early_disp);
  return -1;
}

static int early_get_drm_fb_id(int drm_fd, struct gbm_bo *bo, uint32_t *fb_id) {
  int ret = -1;
  generic_buf_layout_t buf_layout;
  drm_utils::DRMBuffer layout {};
  bool ubwc_status;

  uint32_t alignedWidth = 0;
  ret = gbm_perform(GBM_PERFORM_GET_BO_ALIGNED_WIDTH, bo, &alignedWidth);
  if (ret != GBM_ERROR_NONE) {
    weston_log("Get aligned width failed\n");
    return ret;
  }

  uint32_t alignedHeight = 0;
  ret = gbm_perform(GBM_PERFORM_GET_BO_ALIGNED_HEIGHT, bo, &alignedHeight);
  if (ret != GBM_ERROR_NONE) {
    weston_log("Get aligned height failed\n");
    return ret;
  }

  layout.fd = gbm_bo_get_fd(bo);
  layout.width = alignedWidth;
  layout.height = alignedHeight;
  layout.drm_format = gbm_bo_get_format(bo);

  gbm_perform(GBM_PERFORM_GET_UBWC_STATUS, bo, &ubwc_status);
  if (ubwc_status)
    layout.drm_format_modifier = DRM_FORMAT_MOD_QCOM_COMPRESSED;

  ret = gbm_perform(GBM_PERFORM_GET_PLANE_INFO, bo, &buf_layout);
  if (ret == GBM_ERROR_NONE) {
    layout.num_planes = buf_layout.num_planes;
    for(uint32_t j = 0; j < layout.num_planes; j++) {
      layout.offset[j] = buf_layout.planes[j].offset;
      layout.stride[j] = buf_layout.planes[j].v_increment;
    }
  } else {
    weston_log("Get Plane info failed\n");
    return ret;
  }

  DRMMaster *master = nullptr;
  DRMMaster::GetInstance(&master);

  if(!master) {
    weston_log("Failed to get DRMMaster instance\n");
    return -1;
  }

  ret = master->CreateFbId(layout, fb_id);
  if (ret) {
    weston_log("CreateFbId failed: %m\n");
    return ret;
  }

  if (layout.fd > -1) {
    close(layout.fd);
  }

  return 0;
}

static void early_layer_destroy_callback(struct gbm_bo *bo, void *data) {
  struct early_layer* layer = (struct early_layer*) data;

  if (layer->fb_id) {
    DRMMaster *master = nullptr;
    DRMMaster::GetInstance(&master);

    if(!master) {
      weston_log("Failed to get DRMMaster instance\n");
      return;
    }
    master->RemoveFbId(layer->fb_id);
  }
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
  ret=gbm_perform(GBM_PERFORM_GET_SECURE_BUFFER_STATUS, bo, &layer->secure);
  if (ret) {
    weston_log("gbm get buffer secure status fail\n");
    gbm_bo_destroy(bo);
    return ret;
  }
  weston_buffer_reference(&layer->buffer_ref, buffer);
  gbm_bo_set_user_data(bo, layer, early_layer_destroy_callback);

  return 0;
}

static uint32_t early_search_pipe(display_id disp_id, bool vig_required, bool is_secure, int hw_block_id) {
  std::vector<struct early_plane>::iterator itr = plane_list.begin();
  std::vector<struct early_plane>::iterator itr_backup = plane_list.end();

  for (; itr != plane_list.end(); itr++) {
    //if (!(itr->output_mask & (1 << disp_id)))
    //	continue;
    if (!itr->hw_block_mask.test(hw_block_id))
      continue;
    /* don't consider virtual pipe in early stage */
    if (itr->is_virtual || (itr->block_sec_ui && is_secure))
      continue;
    /*
    * there is a hardware limitation that a pipe should not be
    * assigned to different crtcs in two contiguous commits,
    * so, search a free pipe or a pipe assigned to the same output
    * in last round.
    */
    if ((itr->pre_display_id == DISPMax ||
        itr->pre_display_id == disp_id ) &&
        itr->cur_display_id == DISPMax) {
      /* YUV format needs VIG pipe */
      if (vig_required) {
        if (itr->is_yuv) {
          itr->cur_display_id = disp_id;
          return itr->pipe_id;
        } else
          continue;
        } else {
        /*
        * For RGB format, all DMA/VIG pipes could
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
  struct Rect *src_ret, struct Rect *dst_ret) {
  struct weston_buffer_viewport *viewport = &ev->surface->buffer_viewport;
  pixman_region32_t src_rect, dest_rect;
  pixman_box32_t *box, tbox;
  float sxf1, syf1, sxf2, syf2;

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

  switch(viewport->buffer.transform) {
    case WL_OUTPUT_TRANSFORM_NORMAL: break;
    case WL_OUTPUT_TRANSFORM_90: break;
    case WL_OUTPUT_TRANSFORM_180: break;
    case WL_OUTPUT_TRANSFORM_270: break;
    case WL_OUTPUT_TRANSFORM_FLIPPED: break;
    case WL_OUTPUT_TRANSFORM_FLIPPED_90: break;
    case WL_OUTPUT_TRANSFORM_FLIPPED_180: break;
    case WL_OUTPUT_TRANSFORM_FLIPPED_270: break;
    default:
      weston_log("Invalid buffer transform not supported: %d", viewport->buffer.transform);
      pixman_region32_fini(&src_rect);
      return;
  }

  weston_view_from_global_float(ev, box->x1, box->y1, &sxf1, &syf1);
  weston_surface_to_buffer_float(ev->surface, sxf1, syf1, &sxf1, &syf1);
  weston_view_from_global_float(ev, box->x2, box->y2, &sxf2, &syf2);
  weston_surface_to_buffer_float(ev->surface, sxf2, syf2, &sxf2, &syf2);
  pixman_region32_fini(&src_rect);

  /* Buffer transforms may mean that x2 is to the left of x1, and/or that
   * y2 is above y1. */
  if (sxf2 < sxf1) {
    double tmp = sxf1;
    sxf1 = sxf2;
    sxf2 = tmp;
  }
  if (syf2 < syf1) {
    double tmp = syf1;
    syf1 = syf2;
    syf2 = tmp;
  }

  src_ret->left = (float)(sxf1);
  src_ret->top = (float)(syf1);
  src_ret->right = (float)(sxf2);
  src_ret->bottom = (float)(syf2);
}

static void early_layer_setup_atomic(struct early_layer *layer,
                                     struct drm_output *output,
                                     struct Rect src_rect,
                                     struct Rect dst_rect) {

  struct early_display *early_disp = (struct early_display *)output->early_display_intf;
  DRMAtomicReqInterface *drm_atomic_intf_ = early_disp->drm_atomic_intf_;

  assert(drm_atomic_intf_ != NULL);

  drm_atomic_intf_->Perform(DRMOps::PLANE_SET_CRTC,
                            layer->pipe_id, early_disp->crtc_id);
  drm_atomic_intf_->Perform(DRMOps::PLANE_SET_FB_ID,
                            layer->pipe_id, layer->fb_id);

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
  struct early_layer *layer;
  struct early_display *early_disp =
    (struct early_display *)output->early_display_intf;
  struct Rect src_rect, dst_rect;
  uint32_t pipe_count = 0;
  uint32_t z_order = 0;
  /* true if yuv or scale required*/
  bool vig_required;

  wl_list_for_each_reverse(layer, &output->early_layer_list, link) {
    early_compute_src_dst_rect(output, layer->view, &src_rect, &dst_rect);
    vig_required = layer->yuv_required || (src_rect.right - src_rect.left) !=
      (dst_rect.right - dst_rect.left) || (src_rect.top - src_rect.bottom) !=
      (dst_rect.top - dst_rect.bottom);
    layer->pipe_id = early_search_pipe(display_id(output->display_id),
                                   vig_required, layer->secure, early_disp->hw_block_id);
    if (layer->pipe_id) {
      /*
      * Early display does not support GPU composition.
      * If early layer num is bigger than max_blend_stages,
      * the extra layers are skipped.
      */
      if (z_order++ >= early_disp->max_blend_stages)
        break;
      layer->z_order = z_order;
      layer->hw_block_id = early_disp->hw_block_id;
      early_layer_setup_atomic(layer, output, src_rect, dst_rect);
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
  struct early_layer *layer, *next_layer;
  struct early_display *early_disp =
    (struct early_display *)output->early_display_intf;
  DRMAtomicReqInterface *drm_atomic_intf_ =
      early_disp->drm_atomic_intf_;
  int64_t retire_fence = -1;

  drm_atomic_intf_->Perform(DRMOps::CONNECTOR_GET_RETIRE_FENCE,
    early_disp->connector_id, &retire_fence);

  int ret = drm_atomic_intf_->Commit(false, false);
  if(ret) {
    weston_log("early commit failed\n");
    if(retire_fence > 0)
      close(retire_fence);
    return ret;
  }

  early_reset_planes(display_id(output->display_id));

  output->retire_fence_fd = retire_fence;
  wl_list_for_each_safe(layer, next_layer, &output->early_layer_list, link) {
    layer->sync_handle = dup(retire_fence);
  }
  return 0;
}

/*
 * Unregister early displays in order not to block SDM register
 * display
 */
void early_unregister_display(void *early_display_intf) {
  struct early_display *early_disp =
      (struct early_display *)early_display_intf;

  if(!drm_mgr_intf_)
    return;

  if (!early_disp)
    return;

  if (early_disp->token_.crtc_id)
    drm_mgr_intf_->UnregisterDisplay(&early_disp->token_);
}

void early_destroy_display(void *early_display_intf) {
  struct early_display *early_disp =
      (struct early_display *)early_display_intf;

  if(!drm_mgr_intf_)
    return;

  if (!early_disp)
    return;

  early_unregister_display(early_display_intf);

  if (early_disp->drm_atomic_intf_)
    drm_mgr_intf_->DestroyAtomicReq(early_disp->drm_atomic_intf_);

  free(early_disp);
}

void early_drm_display_deinit(bool destroy) {
  /* DRM Manager is not destroied if sdm is still using it */
  if (destroy) {
    DRMLibLoader *drm_lib_loader = DRMLibLoader::GetInstance();
    if (drm_lib_loader && drm_lib_loader->FuncDestroyDRMManager())
      drm_lib_loader->FuncDestroyDRMManager()();
  }
  drm_mgr_intf_ = nullptr;
  plane_list.clear();
}

#ifdef __cplusplus
}
#endif
