/*
* Copyright (c) 2017, 2021 The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without modification, are permitted
* provided that the following conditions are met:
*    * Redistributions of source code must retain the above copyright notice, this list of
*      conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above copyright notice, this list of
*      conditions and the following disclaimer in the documentation and/or other materials provided
*      with the distribution.
*    * Neither the name of The Linux Foundation nor the names of its contributors may be used to
*      endorse or promote products derived from this software without specific prior written
*      permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
* STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*
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

/*
* Changes from Qualcomm Innovation Center are provided under the following license:
*
* Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted (subject to the limitations in the
* disclaimer below) provided that the following conditions are met:
*
*    * Redistributions of source code must retain the above copyright
*      notice, this list of conditions and the following disclaimer.
*
*    * Redistributions in binary form must reproduce the above
*      copyright notice, this list of conditions and the following
*      disclaimer in the documentation and/or other materials provided
*      with the distribution.
*
*    * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
*      contributors may be used to endorse or promote products derived
*      from this software without specific prior written permission.
*
* NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
* GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
* HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
* IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
* ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
* GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
* IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
* OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <assert.h>
#include <stdarg.h>
#include <linux/limits.h>
#include <string>
#include <vector>
#include <map>
#include <ostream>
#include <fstream>
#include <utility>
#include <bitset>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <cutils/properties.h>

#include "sdm-service/sdm_display.h"
#include "sdm-service/uevent.h"

#include "sdm-internal.h"

#define __CLASS__ "SdmDisplay"
extern "C" void NotifyOnRefresh(struct drm_output *);

vblank_cb_t vblank_cb_;

namespace sdm {
#define GET_GPU_TARGET_SLOT(max_layers) ((max_layers) - 1)
/* Cursor is fixed in (gpu_target_index-1) slot in SDM */
#define GET_CURSOR_SLOT(max_layers) ((max_layers) - 2)
#define LEN_LOCAL 2048

#define SDM_DISPLAY_DUMP_LAYER_STACK 0

#define SDM_DEAFULT_NULL_DISPLAY_WIDTH 1920
#define SDM_DEAFULT_NULL_DISPLAY_HEIGHT 1080
#define SDM_DEAFULT_NULL_DISPLAY_FPS 60
#define SDM_DEAFULT_NULL_DISPLAY_X_DPI 25.4
#define SDM_DEAFULT_NULL_DISPLAY_Y_DPI 25.4
#define SDM_DEAFULT_NULL_DISPLAY_IS_YUV false

#define MAX_PROP_STR_SIZE 64
#define SDM_NULL_DISPLAY_RESOLUTON_PROP_NAME "weston.sdm.default.resolution"
#define SDM_ENABLE_SKIP_PREPARE "vendor.display.enable_skip_prepare"
#define SDM_DISABLE_HDR_HANDLING "vendor.display.disable_hdr"
#define SDM_DISABLE_HDR_TM "vendor.display.disable_hdr_tm"

SdmDisplay::SdmDisplay(DisplayType type, CoreInterface *core_intf,
                                         SdmDisplayBufferAllocator *buffer_allocator) {
    display_type_ = type;
    core_intf_    = core_intf;
    buffer_allocator_ = buffer_allocator;
    drm_output_   = NULL;
    vblank_cb_    = NULL;
}

SdmDisplay::~SdmDisplay() {
    FreeLayerStack();
}

const char * SdmDisplay::FourccToString(uint32_t fourcc)
{
    static __thread char s[4];
    uint32_t fmt = htole32(fourcc);

    memcpy(s, &fmt, 4);

    return s;
}

DisplayError SdmDisplay::CreateDisplay(uint32_t display_id) {
    DisplayError error = kErrorNone;
    struct DisplayHdrInfo display_hdr_info = {};

    char property[PROPERTY_VALUE_MAX];
    property_get(SDM_ENABLE_SKIP_PREPARE, property, "0");
    if (!(strncmp(property, "0", PROPERTY_VALUE_MAX)) ||
        !(strncmp(property, "false", PROPERTY_VALUE_MAX))) {
        disable_skip_prepare_ = 1;
    }

    property_get(SDM_DISABLE_HDR_HANDLING, property, "1");
    if (!(strncmp(property, "0", PROPERTY_VALUE_MAX)) ||
        !(strncmp(property, "false", PROPERTY_VALUE_MAX))) {
        disable_hdr_handling_ = 0;
    }

    error = core_intf_->CreateDisplay(display_id, this, &display_intf_);

    if (error != kErrorNone) {
        DLOGE("Display creation failed. Error = %d", error);
        return error;
    }

    if (disable_hdr_handling_) {
        DLOGI("HDR Handling disabled");
    } else {
        property_get(SDM_DISABLE_HDR_TM, property, "0");
        if (!(strncmp(property, "0", PROPERTY_VALUE_MAX)) ||
            !(strncmp(property, "false", PROPERTY_VALUE_MAX))) {
            disable_tone_mapper_ = 0;
        }
        #ifndef HAS_HDR_SUPPORT
            disable_tone_mapper_ = 1;
        #endif
        if (!disable_tone_mapper_) {
            DLOGI("Tone Mapper Enabled");
            tone_mapper_ = new SdmDisplayToneMapper(buffer_allocator_);

            if (!tone_mapper_)
                DLOGE("Failed to create tone_mapper instance");
        }
    }

    GetHdrInfo(&display_hdr_info);
    if (display_hdr_info.hdr_supported) {
        DLOGI("Display Device supports HDR functionality");
    } else {
        DLOGI("Display Device doesn't support HDR functionality");
    }

    PopulateColorModes();

    return kErrorNone;
}

DisplayError SdmDisplay::DestroyDisplay() {
    DisplayError error;

    error = core_intf_->DestroyDisplay(display_intf_);
    display_intf_ = NULL;

    if (tone_mapper_) {
        delete tone_mapper_;
        tone_mapper_ = nullptr;
    }

    color_mode_map_.clear();

    return error;
}

DisplayError SdmDisplay::VSync(const DisplayEventVSync &vsync) {

#ifndef MULTI_DISPLAY
    if (vblank_cb_)
      vblank_cb_(display_id_, vsync.timestamp, drm_output_);
    else
      DLOGE("vsync not registered");
#endif

    return kErrorNone;
}

DisplayError SdmDisplay::VSync(int fd, unsigned int sequence, unsigned int tv_sec,
                               unsigned int tv_usec, void *data) {

    DLOGW("Not implemented\n");

    return kErrorNone;
}

DisplayError SdmDisplay::PFlip(int fd, unsigned int sequence, unsigned int tv_sec,
                               unsigned int tv_usec, void *data) {

    DLOGW("Not implemented");
    return kErrorNone;
}

DisplayError SdmDisplay::Refresh() {
    if (client_event_handler_) {
        client_event_handler_->Refresh();
    }

    return kErrorNone;
}

DisplayError SdmDisplay::CECMessage(char *message) {
    DLOGW("Not implemented");

    return kErrorNone;
}

DisplayError SdmDisplay::SetDisplayState(DisplayState state,
					 bool teardown, int *release_fence) {
    DisplayError error;

    error = display_intf_->SetDisplayState(state, teardown, release_fence);
    if (error != kErrorNone) {
        DLOGE("function failed. Error = %d", error);
        return error;
    }

    return kErrorNone;
}

DisplayError SdmDisplay::HistogramEvent(int /* fd */, uint32_t /* blob_fd */) {
  return kErrorNone;
}

void SdmDisplay::RefreshWithCachedLayerstack()
{
    DisplayError error = kErrorNone;
    layer_stack_ = prev_layer_stack_;
    error = display_intf_->Prepare(&layer_stack_);
    if (error != kErrorNone) {
        DLOGE("Prepare failed with error %d", error);
        return;
    }

    error = display_intf_->Commit(&layer_stack_);
    if (error != kErrorNone) {
        DLOGE("Commit failed with error %d", error);
        return;
    } else{
        PostCommit(&prev_output_->retire_fence_fd);
        NotifyOnRefresh(prev_output_);
    }
}

void SdmDisplay::HandlePanelDead()
{
    //TODO(user): extend for multi display if needed. currently handle for primary display.
    if (display_type_ != kPrimary) {
      DLOGE("Current display is not primary");
      return;
    }

    esd_reset_panel_ = true;
    int release_fence = -1;
    DisplayError error = kErrorNone;

    DisplayState last_display_state = {};
    error = display_intf_->GetDisplayState(&last_display_state);
    if (error != kErrorNone) {
        DLOGE("Failed to get last display state with error %d", error);
        return;
    }

    error = SetDisplayState(kStateOff, true, &release_fence);
    if (error != kErrorNone) {
        DLOGE("Failed to power off the display with error %d", error);
        return;
    }

    error = SetDisplayState(last_display_state, false, &release_fence);
    if (error != kErrorNone) {
        DLOGE("Failed to SetDisplayState with error %d", error);
        return;
    }

    error = display_intf_->SetVSyncState(true);
    if (error != kErrorNone) {
        DLOGE("Failed to SetVSyncState  with error %d", error);
        return;
    }

    RefreshWithCachedLayerstack();
    esd_reset_panel_ = false;
}

DisplayError SdmDisplay::HandleEvent(DisplayEvent event) {
    switch (event) {
        case kPanelDeadEvent:
            HandlePanelDead();
            break;
        default:
            DLOGW("Unknown event: %d", event);
            break;
    }

    return kErrorNone;
}

DisplayError SdmDisplay::SetVSyncState(bool VSyncState, struct drm_output *output) {
    DisplayError error;

    if (!output) {
        DLOGE("No output to set VSync");
	return kErrorNone;
    }

    drm_output_ = output;
    error = display_intf_->SetVSyncState(VSyncState);
    if (error != kErrorNone) {
        DLOGE("VSync state setting failed. Error = %d", error);
        return error;
    }

    return kErrorNone;
}

DisplayError SdmDisplay::SetPanelBrightness(float brightness) {
    return display_intf_->SetPanelBrightness(brightness);
}

DisplayError SdmDisplay::GetPanelBrightness(float *brightness) {
    return display_intf_->GetPanelBrightness(brightness);
}

DisplayError SdmDisplay::GetDisplayConfiguration(struct DisplayConfigInfo *display_config) {
    DisplayError error = kErrorNone;
    DisplayConfigVariableInfo disp_config;
    uint32_t active_index = 0;

    error = display_intf_->GetActiveConfig(&active_index);

    if (error != kErrorNone) {
        DLOGE("Active Index not found. Error = %d", error);
        return error;
    }

    error = display_intf_->GetConfig(active_index, &disp_config);

    if (error != kErrorNone) {
        DLOGE("Display Configuration failed. Error = %d", error);
        return error;
    }

    display_config->x_pixels     = disp_config.x_pixels;
    display_config->y_pixels     = disp_config.y_pixels;
    display_config->x_dpi        = disp_config.x_dpi;
    display_config->y_dpi        = disp_config.y_dpi;
    display_config->fps          = disp_config.fps;
    display_config->vsync_period_ns = disp_config.vsync_period_ns;
    display_config->is_yuv       = disp_config.is_yuv;
    fps_                         = disp_config.fps;
    display_config->is_connected = true;

    return kErrorNone;
}

DisplayError SdmDisplay::RegisterCb(int display_id, vblank_cb_t vbcb) {
    DisplayError error = kErrorNone;

    vblank_cb_   = vbcb;
    display_id_  = display_id;

    return error;
}

int SdmDisplay::OnMinHdcpEncryptionLevelChange(uint32_t min_enc_level) {
  DisplayError error = display_intf_->OnMinHdcpEncryptionLevelChange(min_enc_level);
  if (error != kErrorNone) {
    DLOGE("Failed. Error = %d", error);
    return -1;
  }

  return 0;
}

DisplayError SdmDisplay::FreeLayerStack() {
    prev_layer_stack_ = layer_stack_;
    for (Layer *layer : layer_stack_.layers) {

         layer->visible_regions.erase(layer->visible_regions.begin(),
                                       layer->visible_regions.end());
         layer->dirty_regions.erase(layer->dirty_regions.begin(),
                                       layer->dirty_regions.end());

         delete layer;
    }
    layer_stack_ = {};

    return kErrorNone;
}

DisplayError SdmDisplay::FreeLayerGeometry(struct LayerGeometry *glayer) {
    if (glayer->dirty_regions.count)
        free(glayer->dirty_regions.rects);
    if (glayer->visible_regions.count)
        free(glayer->visible_regions.rects);

    free(glayer);

    return kErrorNone;
}

DisplayError SdmDisplay::AllocLayerStackMemory(struct drm_output *output) {
    for (size_t i = 0; i < output->view_count; i++) {
         Layer *layer = new Layer();
         layer_stack_.layers.push_back(layer);
    }

    return kErrorNone;
}

static void SetRect(sdm::LayerRect *dst, struct Rect *src)
{
    dst->left = src->left;
    dst->top = src->top;
    dst->right = src->right;
    dst->bottom = src->bottom;
}

DisplayError SdmDisplay::PopulateLayerGeometryOnToLayerStack(struct drm_output *output,
                                                             uint32_t index,
                                                             struct LayerGeometry *glayer,
                                                             bool is_skip) {
    DisplayError error = kErrorNone;
    sdm::Layer *layer = layer_stack_.layers.at(index);
    struct LayerGeometry *layer_geometry = glayer;
    LayerBuffer *layer_buffer, &layer_buffer_ = layer->input_buffer;
    layer_buffer = &layer_buffer_;

    /* 1. Fill buffer information */
    *layer_buffer = sdm::LayerBuffer();
    layer_buffer->format = GetSDMFormat(layer_geometry->format, layer_geometry->flags);
    layer_buffer->width = layer_geometry->width;
    layer_buffer->height = layer_geometry->height;
    layer_buffer->unaligned_width = layer_geometry->unaligned_width;
    layer_buffer->unaligned_height = layer_geometry->unaligned_height;
    /* TODO (user): Obtain metadata and then */
    // TODO: (user)  if (SetMetaData(metadatar layer) != kErrorNone) {
    // TODO: (user)      return kErrorUndefined;
    // TODO: (user)  }

    if (index != layer_stack_.layers.size()-1)
        layer_buffer->planes[0].fd = layer_geometry->ion_fd;

    /* TODO: Below information should be set according to the real user scenario */
    layer_buffer->flags.secure = layer_geometry->flags.secure_present;
    layer_buffer->flags.video = layer_geometry->flags.video_present;
    layer_buffer->flags.hdr = layer_geometry->flags.hdr_present;

    layer_buffer->color_metadata = layer_geometry->color_metadata;

    layer_buffer->flags.macro_tile = false;
    layer_buffer->flags.interlace = false;
    layer_buffer->flags.secure_display = false;
    layer_buffer->flags.secure_camera = false;
    /* 2. Fill layer information */
    if (layer_geometry->composition == SDM_COMPOSITION_FB_TARGET)
        layer->composition = sdm::kCompositionGPUTarget;
    else
        layer->composition = sdm::kCompositionGPU;

    SetRect(&layer->src_rect, &layer_geometry->src_rect);
    SetRect(&layer->dst_rect, &layer_geometry->dst_rect);

    for (uint32_t i = 0; i < layer_geometry->visible_regions.count; i++) {
         SetRect(&layer->visible_regions.at(i), &layer_geometry->visible_regions.rects[i]);
    }

    for (uint32_t i = 0; i < layer_geometry->dirty_regions.count; i++) {
         SetRect(&layer->dirty_regions.at(i), &layer_geometry->dirty_regions.rects[i]);
    }

    layer->blending = GetSDMBlending(layer_geometry->blending);
    layer->plane_alpha = layer_geometry->plane_alpha;

    layer->transform.flip_horizontal =
              ((layer_geometry->transform & SDM_TRANSFORM_FLIP_H) > 0);
    layer->transform.flip_vertical =
              ((layer_geometry->transform & SDM_TRANSFORM_FLIP_V) > 0);
    layer->transform.rotation =
              ((layer_geometry->transform & SDM_TRANSFORM_90) ? 90.0f : 0.0f);

    layer->frame_rate = fps_;
    layer->solid_fill_color = 0;

    layer->flags.skip = is_skip;
    if (is_skip)
       layer_stack_.flags.skip_present = is_skip;
    if (layer_buffer->flags.video)
       layer_stack_.flags.video_present = true;
    if (layer_buffer->flags.hdr)
       layer_stack_.flags.hdr_present = 1;
    if (layer_buffer->flags.secure)
        layer_stack_.flags.secure_present = true;
    layer->flags.updating = true;
    layer->flags.solid_fill = false;
    layer->flags.cursor = false;
    layer->flags.single_buffer = false;

    return error;
}

DisplayError SdmDisplay::AllocateMemoryForLayerGeometry(struct \
                                drm_output *output,
                                uint32_t index,
                                struct LayerGeometry \
                                *glayer) {
    /* Configure each layer */
    uint32_t num_visible_rects = 0;
    uint32_t num_dirty_rects = 0;

    if (index < output->view_count) {
     if (glayer == NULL) {
          DLOGE("no layer geometry for the display!\n");
          return kErrorParameters;
     }
     /* It's permissive the visible/dirty region can be NULL */
     num_visible_rects = glayer->visible_regions.count;
     for (uint32_t j = 0; j < num_visible_rects; j++) {
          LayerRect visible_rect {};
          layer_stack_.layers.at(index)->visible_regions.push_back(visible_rect);
     }

     num_dirty_rects = glayer->dirty_regions.count;
     for (uint32_t j = 0; j < num_dirty_rects; j++) {
          LayerRect dirty_rect {};
          layer_stack_.layers.at(index)->dirty_regions.push_back(dirty_rect);
     }
    }

    return kErrorNone;
}

DisplayError SdmDisplay::AddGeometryLayerToLayerStack(struct drm_output *output, uint32_t index,
                                                      struct LayerGeometry *glayer, bool is_skip)
{
    DisplayError error = kErrorNone;

    AllocateMemoryForLayerGeometry(output, index, glayer);
    error = PopulateLayerGeometryOnToLayerStack(output, index, glayer, is_skip);

    return error;
}

int SdmDisplay::PrepareFbLayerGeometry(struct drm_output *output,
                        struct LayerGeometry **fb_glayer) {
    struct LayerGeometry *fb_layer = NULL;
    *fb_glayer = fb_layer = reinterpret_cast<struct LayerGeometry *> \
                              (zalloc(sizeof *fb_layer));
    if (!fb_layer) {
        DLOGE("fb_layer is NULL\n");
        return -1;
    }

    fb_layer->width = output->base.width;
    fb_layer->height = output->base.height;
    fb_layer->unaligned_width = output->base.width;
    fb_layer->unaligned_height = output->base.height;

    fb_layer->format = GetMappedFormatFromGbm(output->gbm_format);
    fb_layer->composition = SDM_COMPOSITION_FB_TARGET;

    fb_layer->src_rect.left = (float)0.0;
    fb_layer->src_rect.top = (float)0.0;
    fb_layer->src_rect.right = (float)output->base.width;
    fb_layer->src_rect.bottom = (float)output->base.height;
    fb_layer->dst_rect.left = (float)0.0;
    fb_layer->dst_rect.top = (float)0.0;
    fb_layer->dst_rect.right = (float)output->base.width;
    fb_layer->dst_rect.bottom = (float)output->base.height;

    fb_layer->visible_regions.rects = reinterpret_cast<struct Rect *> \
                              (zalloc(sizeof(struct Rect)));
    if (fb_layer->visible_regions.rects == NULL) {
        DLOGE("out of memory for allocating fb visible region\n");
        return -1;
    }
    fb_layer->visible_regions.rects[0] = fb_layer->dst_rect;
    fb_layer->visible_regions.count = 1;

    fb_layer->dirty_regions.rects = reinterpret_cast<struct Rect *> \
                              (zalloc(sizeof(struct Rect)));
    if (fb_layer->dirty_regions.rects == NULL) {
        DLOGE("out of memory for allocating fb dirty region\n");
        return -1;
    }

    fb_layer->dirty_regions.rects[0] = fb_layer->dst_rect;
    fb_layer->dirty_regions.count = 1;

    fb_layer->blending = SDM_BLENDING_PREMULTIPLIED;
    /*
     * output->base.width/height should be already transformed, so just
     * keep no transform for output.
     */
    fb_layer->transform = SDM_TRANSFORM_NORMAL;
    fb_layer->plane_alpha = 0xFF;

    fb_layer->flags.skip = 0;
    fb_layer->flags.is_cursor = 0;
    fb_layer->flags.has_ubwc_buf = false;

    return 0;
}

int SdmDisplayInterface::GetDrmMasterFd() {
    DRMMaster *master = nullptr;
    int ret = DRMMaster::GetInstance(&master);
    int fd;

    if (ret < 0) {
        DLOGE("Failed to acquire DRMMaster instance");
        return kErrorNotSupported;
        }
    master->GetHandle(&fd);
    return fd;
}

int SdmDisplay::PrepareNormalLayerGeometry(struct drm_output *output,
                         struct LayerGeometry **glayer,
                         struct sdm_layer *sdm_layer) {
    struct LayerGeometry *layer;
    struct weston_view *ev = sdm_layer->view;
    struct weston_surface *es = ev->surface;
    bool is_cursor = sdm_layer->is_cursor;
    uint32_t format = GBM_FORMAT_XBGR8888;
    pixman_region32_t r;

    *glayer = layer = reinterpret_cast<struct LayerGeometry *> \
                           (zalloc(sizeof *layer));
    if (!layer) {
        DLOGE("layer is NULL\n");
        return -1;
    }

    /* Prepare layer buffer information */
    layer->width = es->width;
    layer->height = es->height;
    layer->unaligned_width = es->width;
    layer->unaligned_height = es->height;
    layer->fb_id = -1;
    layer->format = SDM_BUFFER_FORMAT_RGBX_8888;

    if (sdm_layer->fb && sdm_layer->fb->bo) {
        struct gbm_bo *bo = sdm_layer->fb->bo;
        if (bo == NULL) {
            DLOGE("fail to import gbm bo!\n");
            return -1;
        } else {
            uint32_t width, height;

            //save gbm bo in sdm layer for future reference.
            sdm_layer->bo = bo;

            width = gbm_bo_get_width(bo);
            height = gbm_bo_get_height(bo);
            format = gbm_bo_get_format(bo);

            uint32_t alignedWidth = 0;
            uint32_t alignedHeight = 0;
            uint32_t secure_status = 0;
            uint32_t ubwc_status = 0;
            void *prm = reinterpret_cast<void *> (&layer->color_metadata);

            gbm_perform(GBM_PERFORM_GET_BO_ALIGNED_WIDTH, bo, &alignedWidth);
            gbm_perform(GBM_PERFORM_GET_BO_ALIGNED_HEIGHT, bo, &alignedHeight);
            gbm_perform(GBM_PERFORM_GET_SECURE_BUFFER_STATUS, bo, &secure_status);
            gbm_perform(GBM_PERFORM_GET_METADATA, bo, GBM_METADATA_GET_COLOR_METADATA, prm);
            gbm_perform(GBM_PERFORM_GET_UBWC_STATUS, bo, &ubwc_status);

            // Override buffer width/height to reflect aligned width and aligned height.
            layer->width = alignedWidth;
            layer->height = alignedHeight;
            layer->unaligned_width = width;
            layer->unaligned_height = height;
            layer->ion_fd = sdm_layer->fb->ion_fd;
            layer->flags.secure_present = secure_status;
            layer->flags.has_ubwc_buf = ubwc_status;

            bool hdr_layer = layer->color_metadata.colorPrimaries == ColorPrimaries_BT2020 &&
                             (layer->color_metadata.transfer == Transfer_SMPTE_ST2084 ||
                             layer->color_metadata.transfer == Transfer_HLG);

            // Set to true if incoming layer has HDR support and Display supports HDR functionality
            if (!disable_hdr_handling_) {
                layer->flags.hdr_present = hdr_layer;
            }
        }
    }

    if (NeedConvertGbmFormat(ev, format))
        format = ConvertToOpaqueGbmFormat(format);
    layer->format = GetMappedFormatFromGbm(format);

    /* Get src/dst rect */
    ComputeSrcDstRect(output, ev, &layer->src_rect, &layer->dst_rect);
    /* Get visible rects */
    if (GetVisibleRegion(output, ev, &sdm_layer->overlap, &layer->visible_regions))
        return -1;
    /*
     * Get dirty rects. It's not surprised a view has null damage region, that means
     * the buffer content of view is not changed since last frame, eg. cursor.
     */
    if (ComputeDirtyRegion(ev, &layer->dirty_regions))
        return -1;

    /* Set no transform for view since bounding box already been applied by transform */
    layer->transform = SDM_TRANSFORM_NORMAL;
    /* Get global alpha */
    layer->plane_alpha = GetGlobalAlpha(ev);

    /* TODO: update property src_config, csc, color_fill, scaler ... */
    layer->flags.is_cursor = is_cursor;
    layer->flags.video_present = GetVideoPresenceByFormatFromGbm(format);

    /* compute whether this view has no blending */
    pixman_region32_init_rect(&r, 0, 0, ev->surface->width, ev->surface->height);
    pixman_region32_subtract(&r, &r, &ev->surface->opaque);

    if (!pixman_region32_not_empty(&r) && (layer->plane_alpha == 0xFF))
        layer->blending = SDM_BLENDING_NONE;
    else
        layer->blending = SDM_BLENDING_COVERAGE;

    // Video layers are always opaque
    if (layer->flags.video_present) {
        layer->blending = SDM_BLENDING_NONE;
    }

    /* Initialize all views with GPU composition first, SDM will update them after prepare */
    layer->composition = SDM_COMPOSITION_GPU;

    return 0;
}

void SdmDisplay::PopulateColorModes() {
    uint32_t color_mode_count = 0;
    // SDM returns modes which have attributes defining mode and rendering intent
    DisplayError error = display_intf_->GetColorModeCount(&color_mode_count);
    if (error != kErrorNone || (color_mode_count == 0)) {
        DLOGW("GetColorModeCount failed, use native color mode");
        color_mode_map_[ColorPrimaries_BT709_5][Transfer_sRGB] = "hal_native_identity";
        return;
    }

    DLOGI("Color Modes supported count = %d", color_mode_count);

    std::vector<std::string> color_modes(color_mode_count);
    error = display_intf_->GetColorModes(&color_mode_count, &color_modes);
    for (uint32_t i = 0; i < color_mode_count; i++) {
        std::string &mode_string = color_modes.at(i);
        DLOGI("Color Mode[%d] = %s", i, mode_string.c_str());
        AttrVal attr = {};
        error = display_intf_->GetColorModeAttr(mode_string, &attr);
        std::string color_gamut = kNative, dynamic_range = kSdr,
                pic_quality = kStandard, transfer = kSrgb;
        int int_render_intent = -1;
        if (!attr.empty()) {
            for (auto &it : attr) {
                if (it.first.find(kColorGamutAttribute) != std::string::npos) {
                    color_gamut = it.second;
                } else if (it.first.find(kDynamicRangeAttribute) != std::string::npos) {
                    dynamic_range = it.second;
                } else if (it.first.find(kPictureQualityAttribute) != std::string::npos) {
                    pic_quality = it.second;
                } else if (it.first.find(kGammaTransferAttribute) != std::string::npos) {
                    transfer = it.second;
                } else if (it.first.find(kRenderIntentAttribute) != std::string::npos) {
                    int_render_intent = std::stoi(it.second);
                }
            }

            // TODO: Check if it is relevant
            if (int_render_intent < 0 || int_render_intent > MAX_EXTENDED_RENDER_INTENT) {
                DLOGW("Invalid render intent %d for mode %s",
                        int_render_intent, mode_string.c_str());
                continue;
            }
            DLOGI("color_gamut : %s, dynamic_range : %s, pic_quality : %s, "
                "render_intent : %d", color_gamut.c_str(), dynamic_range.c_str(),
                pic_quality.c_str(), int_render_intent);

            if (color_gamut == kNative) {
                color_mode_map_[ColorPrimaries_BT709_5][Transfer_sRGB] = mode_string;
            }

            if (color_gamut == kSrgb && dynamic_range == kSdr) {
                color_mode_map_[ColorPrimaries_BT709_5][Transfer_sRGB] = mode_string;
            }

            if (color_gamut == kDcip3 && dynamic_range == kSdr) {
                color_mode_map_[ColorPrimaries_DCIP3][Transfer_sRGB] = mode_string;
            }
            if (color_gamut == kBt2020) {
                if (transfer == kSt2084) {
                    color_mode_map_[ColorPrimaries_BT2020][Transfer_SMPTE_ST2084] = mode_string;
                } else if (transfer == kHlg) {
                    color_mode_map_[ColorPrimaries_BT2020][Transfer_HLG] = mode_string;
                } else if (transfer == kSrgb) {
                    color_mode_map_[ColorPrimaries_BT2020][Transfer_sRGB] = mode_string;
                }
            }
        } else {
            // Look at the mode names, if no attributes are found
            if (mode_string.find("hal_native") != std::string::npos) {
                color_mode_map_[ColorPrimaries_BT709_5][Transfer_sRGB] = mode_string;
            }
        }
    }
}

DisplayError SdmDisplay::ValidateColorMode(PrimariesTransfer color_mode) {
    ColorPrimaries primary = color_mode.primaries;
    GammaTransfer transfer = color_mode.transfer;
    if (primary < ColorPrimaries_BT709_5 || primary >= ColorPrimaries_Max) {
        DLOGE("Invalid color primary: %d", primary);
        return kErrorParameters;
    }
    if (color_mode_map_.find(primary) == color_mode_map_.end()) {
        DLOGE("Could not find color primary: %d", primary);
        return kErrorNotSupported;
    }
    if (color_mode_map_[primary].find(transfer) == color_mode_map_[primary].end()) {
        DLOGE("Could not find transfer %d in primary %d", transfer, primary);
        return kErrorNotSupported;
    }
    return kErrorNone;
}

DisplayError SdmDisplay::SetColorMode(PrimariesTransfer color_mode) {
    if (current_color_mode_ == color_mode) {
        return kErrorNone;
    }

    DisplayError error = ValidateColorMode(color_mode);
    if (error != kErrorNone) {
        return error;
    }

    auto mode_string = color_mode_map_[color_mode.primaries][color_mode.transfer];
    error = display_intf_->SetColorMode(mode_string);
    if (error != kErrorNone) {
        DLOGE("failed for primary = %d transfer = %d name = %s",
            color_mode.primaries, color_mode.transfer, mode_string.c_str());
        return kErrorNotSupported;
    }

    current_color_mode_ = color_mode;
    DLOGV("Successfully applied primary = %d transfer = %d name = %s",
           color_mode.primaries, color_mode.transfer, mode_string.c_str());
    return kErrorNone;
}

PrimariesTransfer SdmDisplay::SelectBestColorSpace(bool isHdrSupported) {
    PrimariesTransfer best_color_mode = {};
    PrimariesTransfer best_hdr_color_mode = {};
    for (uint32_t i = 0; i < layer_stack_.layers.size(); i++) {
        struct Layer *layer = nullptr;
        struct LayerBuffer buffer = {};
        layer = layer_stack_.layers.at(i);
        buffer = layer->input_buffer;

        if (layer->flags.skip) {
            continue;
        }

        PrimariesTransfer color_mode = {};
        color_mode.primaries = buffer.color_metadata.colorPrimaries;
        color_mode.transfer = buffer.color_metadata.transfer;

        switch (color_mode.primaries) {
            case ColorPrimaries_DCIP3:
                best_color_mode.primaries = ColorPrimaries_DCIP3;
                best_color_mode.transfer = Transfer_sRGB;
                break;
            case ColorPrimaries_BT2020:
                if (color_mode.transfer == Transfer_sRGB) {
                    best_color_mode.primaries = ColorPrimaries_BT2020;
                    best_color_mode.transfer = Transfer_sRGB;
                } else if (color_mode.transfer == Transfer_SMPTE_ST2084) {
                    best_hdr_color_mode = color_mode;
                } else if (color_mode.transfer == Transfer_HLG &&
                    best_hdr_color_mode.transfer != Transfer_SMPTE_ST2084) {
                    best_hdr_color_mode = color_mode;
                }
                break;
            default:
                break;
        }
    }
    return isHdrSupported ? best_hdr_color_mode : best_color_mode;
}

DisplayError SdmDisplay::PrePrepareLayerStack(struct drm_output *output) {
    DisplayError error = kErrorNone;
    struct sdm_layer *sdm_layer = NULL;
    struct LayerGeometry *glayer = NULL;
    uint32_t gpu_target_index = GET_GPU_TARGET_SLOT(output->view_count);
    uint32_t index = 0;
    LayerBufferFlags layerBufferFlags;

    if (shutdown_pending_) {
    	return kErrorShutDown;
    }

    FreeLayerStack();
    AllocLayerStackMemory(output);

    DLOGI("gpu_target_index = %d\n", gpu_target_index);

    /* If no view can be handled by SDM, just skip below and prepare fb target directly. */
    if (gpu_target_index > 0) {
        wl_list_for_each_reverse(sdm_layer, &output->sdm_layer_list, link) {
            glayer = NULL;
            if(PrepareNormalLayerGeometry(output, &glayer, sdm_layer)) {
                DLOGE("fail to prepare normal layer geometry.");
                return kErrorUndefined;
            }

            error = AddGeometryLayerToLayerStack(output, index, glayer, sdm_layer->is_skip);
            if (error) {
                DLOGE("failed add Geometry Layer to LayerStack.");
                FreeLayerGeometry(glayer);
                return kErrorUndefined;
            }
            FreeLayerGeometry(glayer);

            // Pass the wl_resource handle from sdm layer to layer stack
            // to use it for egl image creation in tone mapping
            layerBufferFlags = layer_stack_.layers.at(index)->input_buffer.flags;
            if (layerBufferFlags.video && layerBufferFlags.hdr) {
                layer_stack_.layers.at(index)->userdata =
                        sdm_layer->view->surface->buffer_ref.buffer->resource;
            }

            //Acquire fence fd is not received for frame buffer target layer as gl-renderer is not
            //sending it. Hence not adding acquire fence for frame buffer target
            layer_stack_.layers.at(index)->input_buffer.acquire_fence_fd =
                                                      sdm_layer->acquire_fence_fd;

            index++;
            if (sdm_layer->is_skip)
                layer_stack_.flags.skip_present = true;
        }
    }
    int err = PrepareFbLayerGeometry(output, &glayer);
    if (err) {
        DLOGE("failed to prepare Layer Geometry Fb target\n");
        return kErrorUndefined;
    } else {
        error = AddGeometryLayerToLayerStack(output, index, glayer, false);
        if (error) {
            DLOGE("fail to prepare Fb target: Add Geometry failure fb\n");
            FreeLayerGeometry(glayer);
            return kErrorUndefined;
        }
        FreeLayerGeometry(glayer);
    }

    return error;
}

DisplayError SdmDisplay::PrePrepare(struct drm_output *output)
{
    DisplayError error = kErrorNone;

    error = PrePrepareLayerStack(output);
    if (error ) {
        DLOGE("function failed!\n", error);
    }

    if (hdr_supported_ && !disable_hdr_handling_) {
        PrimariesTransfer best_color_mode = SelectBestColorSpace(hdr_supported_);
        error = SetColorMode(best_color_mode);
        if (error != kErrorNone) {
            DLOGE("Setting color mode failed.");
        }
    }

    return error;
}

DisplayError SdmDisplay::PostPrepare(struct drm_output *output)
{
    DisplayError error = kErrorNone;
    int index = 0;
    struct sdm_layer *sdm_layer = NULL;

    wl_list_for_each_reverse(sdm_layer, &output->sdm_layer_list, link) {
      Layer *layer = layer_stack_.layers.at(index);

      if (layer->composition == kCompositionGPU) {
        sdm_layer->composition_type = SDM_COMPOSITION_GPU;
      } else {
        sdm_layer->composition_type = SDM_COMPOSITION_OVERLAY;
      }
      index++;
    }

    return error;
}

#if SDM_DISPLAY_DUMP_LAYER_STACK
static void GetLayerStackDump(void *layerStack, char *buffer, uint32_t length) {
  if (!buffer || !length) {
    return;
  }
  static int frame_count=0;
  buffer[0] = '\0';
  struct LayerStack *layer_stack;
  layer_stack = reinterpret_cast<struct LayerStack *>(layerStack);
  DLOGI("\n-------- Display Manager: Layer Stack Dump --------");
  fprintf(stderr,"Frame:%d LayerStack: NumLayers:%d flags:0x%x\n",
                frame_count, layer_stack->layers.size(), layer_stack->flags);

  for (uint32_t i = 0; i < layer_stack->layers.size(); i++) {
      char buf[LEN_LOCAL] = {0};

      struct Layer *layer;
      struct LayerBuffer buffer;
      layer = layer_stack->layers.at(i);
      buffer = layer->input_buffer;

      memset(buf, '\0', LEN_LOCAL);
      snprintf(buf, sizeof(buf), "Layer: %d\n    width  = %d,     height = %d", i,
        layer->input_buffer.width, layer->input_buffer.height);
      snprintf(buf, sizeof(buf), "%s\n LayerComposition = %#x", buf, layer->composition);
      snprintf(buf, sizeof(buf), "%s\n src_rect (LTRB) = %4.2f, %4.2f, %4.2f, %4.2f",
        buf, layer->src_rect.left, layer->src_rect.top,
        layer->src_rect.right, layer->src_rect.bottom);
      snprintf(buf, sizeof(buf), "%s\n dst_rect (LTRB) = %4.2f, %4.2f, %4.2f, %4.2f", buf,
        layer->dst_rect.left, layer->dst_rect.top, layer->dst_rect.right,
        layer->dst_rect.bottom);
      snprintf(buf, sizeof(buf), "%s\n LayerBlending = %#x", buf, layer->blending);
      snprintf(buf, sizeof(buf), "%s\n LayerTransform:rotation= %f,flip_horizontal=%s,flip_vertical=%s",
        buf, layer->transform.rotation, (layer->transform.flip_horizontal? \
        "true":"false"), (layer->transform.flip_vertical? "true":"false"));
      snprintf(buf, sizeof(buf), "%s\n Plane Alpha = %#x, frame_rate = %d,  solid_fill_color = %d",
        buf, layer->plane_alpha, layer->frame_rate, layer->solid_fill_color);
      snprintf(buf, sizeof(buf), "%s\n LayerFlags = %#x", buf, layer->flags);
      snprintf(buf, sizeof(buf), "%s\t LayerFlags.skip = %d", buf, layer->flags.skip);
      snprintf(buf, sizeof(buf), "%s\n LayerBuffer Flags: hdr:%d secure:%d video:%d", buf,
                    buffer.flags.hdr, buffer.flags.secure, buffer.flags.video);

      fprintf(stderr,"\n%s\n", buf);
  }
  frame_count++;

  return;
}
#endif

bool SdmDisplay::CanSkipPrepare() {
    if (disable_skip_prepare_) {
        return false;
    }

    int prev_layer_count = prev_layer_stack_.layers.size();
    int curr_layer_count = layer_stack_.layers.size();
    if (prev_layer_count != curr_layer_count) {
        return false;
    }

    if ((prev_layer_stack_.flags.skip_present != layer_stack_.flags.skip_present) ||
        (prev_layer_stack_.flags.video_present != layer_stack_.flags.video_present) ||
        (prev_layer_stack_.flags.hdr_present != layer_stack_.flags.hdr_present) ||
        (prev_layer_stack_.flags.secure_present != layer_stack_.flags.secure_present)) {
        return false;
    }

    for (uint32_t i = 0; i < curr_layer_count; i++) {
        Layer *curr_layer = layer_stack_.layers.at(i);
        Layer *prev_layer = prev_layer_stack_.layers.at(i);
        LayerBuffer curr_buffer = curr_layer->input_buffer;
        LayerBuffer prev_buffer = prev_layer->input_buffer;
        if ((curr_layer->visible_regions.size() != prev_layer->visible_regions.size()) ||
            (curr_layer->dirty_regions.size() != prev_layer->dirty_regions.size())) {
            return false;
        }

        if ((curr_layer->blending != prev_layer->blending) ||
            (curr_layer->transform != prev_layer->transform) ||
            (curr_layer->plane_alpha != prev_layer->plane_alpha) ||
            (curr_layer->flags.skip != prev_layer->flags.skip) ||
            (curr_layer->frame_rate != prev_layer->frame_rate)) {
            return false;
        }

        if ((curr_layer->src_rect != prev_layer->src_rect) ||
            (curr_layer->dst_rect != prev_layer->dst_rect )) {
            return false;
        }

        if ((curr_layer->solid_fill_color != prev_layer->solid_fill_color) ||
            (curr_layer->solid_fill_info.bit_depth != prev_layer->solid_fill_info.bit_depth) ||
            (curr_layer->solid_fill_info.red != prev_layer->solid_fill_info.red) ||
            (curr_layer->solid_fill_info.green != prev_layer->solid_fill_info.green) ||
            (curr_layer->solid_fill_info.blue != prev_layer->solid_fill_info.blue) ||
            (curr_layer->solid_fill_info.alpha != prev_layer->solid_fill_info.alpha)) {
            return false;
        }

        if ((curr_buffer.width != prev_buffer.width) ||
            (curr_buffer.height != prev_buffer.height) ||
            (curr_buffer.unaligned_width != prev_buffer.unaligned_width) ||
            (curr_buffer.unaligned_height != prev_buffer.unaligned_height) ||
            (curr_buffer.size != prev_buffer.size) ||
            (curr_buffer.format != prev_buffer.format)) {
            return false;
        }

        if ((curr_buffer.flags.secure != prev_buffer.flags.secure) ||
            (curr_buffer.flags.video != prev_buffer.flags.video) ||
            (curr_buffer.flags.hdr != prev_buffer.flags.hdr) ||
            (curr_buffer.color_metadata.colorPrimaries !=
                prev_buffer.color_metadata.colorPrimaries) ||
            (curr_buffer.color_metadata.transfer != prev_buffer.color_metadata.transfer)) {
            return false;
        }
    }

    /*
        TODO: Add checks for following metadata regarding layers:
        1. LUT3D
        2. Color Transform Matrix
        3. Layer Update Mask
    */
    return true;
}

DisplayError SdmDisplay::Prepare(struct drm_output *output)
{
    DisplayError error = kErrorNone;

    if (esd_reset_panel_) {
      return kErrorNotSupported;
    }

#if SDM_DISPLAY_DUMP_LAYER_STACK
    char dump_buffer[8192] = {0};
#endif

    error = PrePrepare(output);
    if (error != kErrorNone) {
        DLOGE("failed during PrePrepare");
    }

#if SDM_DISPLAY_DUMP_LAYER_STACK
    // Dump all input layers of the layer stack:
    GetLayerStackDump(&layer_stack_, dump_buffer, sizeof(dump_buffer));
#endif

    if (!CanSkipPrepare()) {
        error = display_intf_->Prepare(&layer_stack_);
        if (error != kErrorNone) {
            DLOGE("failed during Prepare");
        }
    }

#if SDM_DISPLAY_DUMP_LAYER_STACK
    DumpInterface::GetDump(dump_buffer, sizeof(dump_buffer));
#endif

    error = PostPrepare(output);
    if (error != kErrorNone)
    	DLOGE("function failed Error= %d\n", error);

    return error;
}

DisplayError SdmDisplay::PreCommit()
{
    DisplayError error = kErrorNone;
    if (layer_stack_.flags.hdr_present) {
        int status = -1;
        if (tone_mapper_) {
            error = tone_mapper_->HandleToneMap(&layer_stack_);
            if (error != kErrorNone) {
                DLOGE("Error handling HDR in ToneMapper, status code = %d", status);
            }
        } else {
            DLOGD("HandleToneMap failed due to invalid tone_mapper_ instance");
        }
    } else {
        if (tone_mapper_)
            tone_mapper_->Terminate();
        else
            DLOGD("ToneMap Terminate failed due to invalid tone_mapper_ instance");
    }

    return error;
}


DisplayError SdmDisplay::PostCommit(int *retire_fence_fd)
{
    DisplayError error = kErrorNone;

    if (tone_mapper_ && tone_mapper_->IsActive()) {
        tone_mapper_->PostCommit(&layer_stack_);
    }

    prev_layer_stack_ = layer_stack_;
    //Iterate through the layer buffer and close release fences
    for (uint32_t i = 0; i < layer_stack_.layers.size(); i++) {
        Layer *layer = layer_stack_.layers.at(i);
        LayerBuffer *layer_buffer = &layer->input_buffer;

        if (layer_buffer->release_fence_fd > 0) {
          close(layer_buffer->release_fence_fd);
          layer_buffer->release_fence_fd = -1;
        }
    }

    //close release fence fds
    if (layer_stack_.retire_fence_fd > 0) {
      *retire_fence_fd = previous_retire_fence_fd_;
      previous_retire_fence_fd_ = layer_stack_.retire_fence_fd;
      layer_stack_.retire_fence_fd = -1;
    }

    return error;
}

DisplayError SdmDisplay::Commit(struct drm_output *output)
{
    DTRACE_SCOPED();
    DisplayError ret = kErrorNone;

    if (esd_reset_panel_) {
      return kErrorNotSupported;
    }

    uint32_t layer_count = layer_stack_.layers.size();
    uint32_t GPUTarget_index = layer_count-1;
    Layer *GpuTargetlayer;

    if (!output->next_fb) {
	DLOGE("Scanout state fb not found for output=%d", output->base.id);
	return kErrorUndefined;
    }

    GpuTargetlayer = layer_stack_.layers.at(GPUTarget_index);
    GpuTargetlayer->input_buffer.planes[0].fd = output->next_fb->ion_fd;

    DLOGI("commiting ion fd = %d", output->next_fb->ion_fd);

    ret = PreCommit();
    if (ret != kErrorNone) {
        DLOGE("PreCommit failed!");
        ret = kErrorNone;
    }

    prev_output_ = output;
    ret = display_intf_->Commit(&layer_stack_);

    PostCommit(&output->retire_fence_fd);

    return ret;
}

/* Adding following  support functions */
LayerBufferFormat SdmDisplay::GetSDMFormat(uint32_t src_fmt, struct LayerGeometryFlags flags)
{
    LayerBufferFormat format = kFormatInvalid;

    if (flags.has_ubwc_buf) {
        switch (src_fmt) {
            case SDM_BUFFER_FORMAT_RGBA_8888:
                format = sdm::kFormatRGBA8888Ubwc;
                break;
            case SDM_BUFFER_FORMAT_RGBX_8888:
                format = sdm::kFormatRGBX8888Ubwc;
                break;
            case SDM_BUFFER_FORMAT_BGR_565:
                format = sdm::kFormatBGR565Ubwc;
                break;
            case SDM_BUFFER_FORMAT_RGBA_2101010:
                format = sdm::kFormatRGBA1010102Ubwc;
                break;
            case SDM_BUFFER_FORMAT_YCbCr_420_SP_VENUS:
            case SDM_BUFFER_FORMAT_NV12_ENCODEABLE:
                format = sdm::kFormatYCbCr420SPVenusUbwc;
                break;
            case SDM_BUFFER_FORMAT_YCbCr_420_TP10_UBWC:
                format = sdm::kFormatYCbCr420TP10Ubwc;
                break;
            case SDM_BUFFER_FORMAT_YCbCr_420_P010_UBWC:
                format = sdm::kFormatYCbCr420P010Ubwc;
                break;
            default:
                DLOGE("Unsupported UBWC format %d\n", src_fmt);
                return sdm::kFormatInvalid;
        }

        return format;
    }

    switch (src_fmt) {
        case SDM_BUFFER_FORMAT_RGBA_8888:
            format = sdm::kFormatRGBA8888;
            break;
        case SDM_BUFFER_FORMAT_RGBA_5551:
            format = sdm::kFormatRGBA5551;
            break;
        case SDM_BUFFER_FORMAT_RGBA_4444:
            format = sdm::kFormatRGBA4444;
            break;
        case SDM_BUFFER_FORMAT_BGRA_8888:
            format = sdm::kFormatBGRA8888;
            break;
        case SDM_BUFFER_FORMAT_RGBX_8888:
            format = sdm::kFormatRGBX8888;
            break;
        case SDM_BUFFER_FORMAT_BGRX_8888:
            format = sdm::kFormatBGRX8888;
            break;
        case SDM_BUFFER_FORMAT_RGB_888:
            format = sdm::kFormatRGB888;
            break;
        case SDM_BUFFER_FORMAT_RGB_565:
            format = sdm::kFormatRGB565;
            break;
        case SDM_BUFFER_FORMAT_BGR_565:
            format = sdm::kFormatBGR565;
            break;
        case SDM_BUFFER_FORMAT_NV12_ENCODEABLE:
        case SDM_BUFFER_FORMAT_YCbCr_420_SP_VENUS:
            format = sdm::kFormatYCbCr420SemiPlanarVenus;
            break;
        case SDM_BUFFER_FORMAT_ABGR_2101010:
            format = sdm::kFormatABGR2101010;
            break;
        case SDM_BUFFER_FORMAT_RGBA_2101010:
            format = sdm::kFormatRGBA1010102;
            break;
        case SDM_BUFFER_FORMAT_YCbCr_420_TP10_UBWC:
            format = sdm::kFormatYCbCr420TP10Ubwc;
            break;
        case SDM_BUFFER_FORMAT_YCbCr_420_P010_UBWC:
            format = sdm::kFormatYCbCr420P010Ubwc;
            break;
        case SDM_BUFFER_FORMAT_YV12:
            format = sdm::kFormatYCrCb420PlanarStride16;
            break;
        case SDM_BUFFER_FORMAT_YCrCb_420_SP:
            format = sdm::kFormatYCrCb420SemiPlanar;
            break;
        case SDM_BUFFER_FORMAT_YCbCr_420_SP:
            format = sdm::kFormatYCbCr420SemiPlanar;
            break;
        case SDM_BUFFER_FORMAT_YCbCr_422_SP:
            format = sdm::kFormatYCbCr422H2V1SemiPlanar;
            break;
        case SDM_BUFFER_FORMAT_YCbCr_422_I:
            format = sdm::kFormatYCbCr422H2V1Packed;
            break;
        case SDM_BUFFER_FORMAT_P010:
            format = sdm::kFormatYCbCr420P010;
            break;
        default:
            DLOGE("Unsupported format %d\n", src_fmt);
            return sdm::kFormatInvalid;
    }

    return format;
}

LayerBlending SdmDisplay::GetSDMBlending(uint32_t source)
{
    sdm::LayerBlending blending = sdm::kBlendingPremultiplied;

    switch (source) {
    case SDM_BLENDING_PREMULTIPLIED:
         blending = sdm::kBlendingPremultiplied;
         break;
    case SDM_BLENDING_COVERAGE:
         blending = sdm::kBlendingCoverage;
         break;
    case SDM_BLENDING_NONE:
    default:
         blending = sdm::kBlendingOpaque;
         break;
    }

    return blending;
}

bool SdmDisplay::GetVideoPresenceByFormatFromGbm(uint32_t fmt)
{
    bool is_video_present = false;

    switch (fmt) {
       case GBM_FORMAT_NV12:
       case GBM_FORMAT_YCbCr_420_TP10_UBWC:
       case GBM_FORMAT_YCbCr_420_P010_UBWC:
       case GBM_FORMAT_P010:
            is_video_present = true;
            break;
       default:
            is_video_present = false;
            break;
    }

    return is_video_present;
}

uint32_t SdmDisplay::GetMappedFormatFromGbm(uint32_t fmt)
{
    uint32_t ret = SDM_BUFFER_FORMAT_INVALID;

    switch (fmt) {
    case GBM_FORMAT_ARGB8888:
         ret = SDM_BUFFER_FORMAT_BGRA_8888;
         break;
    case GBM_FORMAT_XRGB8888:
         ret = SDM_BUFFER_FORMAT_BGRX_8888;
         break;
    case GBM_FORMAT_ABGR8888:
         ret = SDM_BUFFER_FORMAT_RGBA_8888;
         break;
    case GBM_FORMAT_XBGR8888:
         ret = SDM_BUFFER_FORMAT_RGBX_8888;
         break;
    case GBM_FORMAT_BGRA8888:
    case GBM_FORMAT_RGBA8888:
         ret = SDM_BUFFER_FORMAT_ARGB_8888;
         break;
    case GBM_FORMAT_ARGB4444:
    case GBM_FORMAT_ABGR4444:
         ret = SDM_BUFFER_FORMAT_RGBA_4444;
         break;
    case GBM_FORMAT_ARGB1555:
    case GBM_FORMAT_ABGR1555:
         ret = SDM_BUFFER_FORMAT_RGBA_5551;
         break;
    case GBM_FORMAT_RGB565:
         ret = SDM_BUFFER_FORMAT_BGR_565;
         break;
    case GBM_FORMAT_BGR565:
         ret = SDM_BUFFER_FORMAT_RGB_565;
         break;
    case GBM_FORMAT_RGB888:
         ret = SDM_BUFFER_FORMAT_BGR_888;
         break;
    case GBM_FORMAT_BGR888:
         ret = SDM_BUFFER_FORMAT_RGB_888;
         break;
    case GBM_FORMAT_NV12:
         ret = SDM_BUFFER_FORMAT_YCbCr_420_SP_VENUS;
         break;
    case GBM_FORMAT_ABGR2101010:
         ret = SDM_BUFFER_FORMAT_RGBA_2101010;
         break;
    case GBM_FORMAT_YCbCr_420_TP10_UBWC:
         ret = SDM_BUFFER_FORMAT_YCbCr_420_TP10_UBWC;
         break;
    case GBM_FORMAT_YCbCr_420_P010_UBWC:
        ret = SDM_BUFFER_FORMAT_YCbCr_420_P010_UBWC;
        break;
    case GBM_FORMAT_P010:
        ret = SDM_BUFFER_FORMAT_P010;
        break;
    default:
         DLOGE("Unsupported GBM format %s\n", FourccToString(fmt));
         break;
    }

    return ret;
}

bool SdmDisplay::NeedConvertGbmFormat(struct weston_view *ev, uint32_t format)
{
    pixman_region32_t r;
    bool need_convert = false;

    if (IsTransparentGbmFormat(format)) {
     pixman_region32_init_rect(&r, 0, 0,
             ev->surface->width,
             ev->surface->height);
     pixman_region32_subtract(&r, &r, &ev->surface->opaque);

     if (!pixman_region32_not_empty(&r))
         need_convert = true;

     pixman_region32_fini(&r);
    }

    return need_convert;
}

uint32_t SdmDisplay::ConvertToOpaqueGbmFormat(uint32_t format)
{
    uint32_t ret = GBM_FORMAT_XRGB8888;

    switch (format) {
    case GBM_FORMAT_ARGB8888:
     ret = GBM_FORMAT_XRGB8888;
     break;
    case GBM_FORMAT_BGRA8888:
     ret = GBM_FORMAT_BGRX8888;
     break;
    case GBM_FORMAT_RGBA8888:
     ret = GBM_FORMAT_RGBX8888;
     break;
    case GBM_FORMAT_ABGR8888:
     ret = GBM_FORMAT_XBGR8888;
     break;
    default:
     break;
    }

    return ret;
}

void SdmDisplay::ComputeSrcDstRect(struct drm_output *output, struct weston_view *ev,
                    struct Rect *src_ret, struct Rect *dst_ret)
{
    pixman_region32_t src_rect, dest_rect;
    float sx1, sy1, sx2, sy2;

    /* dst rect */
    pixman_region32_init(&dest_rect);
    pixman_region32_intersect(&dest_rect, &ev->transform.boundingbox, &output->base.region);

    pixman_region32_translate(&dest_rect, -output->base.x, -output->base.y);

    pixman_box32_t *box, tbox = {0};
    box = pixman_region32_extents(&dest_rect);

    sdm_weston_transformed_rect(output, &tbox, *box);

    dst_ret->left = tbox.x1;
    dst_ret->right = tbox.x2;
    dst_ret->top = tbox.y1;
    dst_ret->bottom = tbox.y2;

    pixman_region32_fini(&dest_rect);

    /* src rect */
    pixman_region32_init(&src_rect);
    pixman_region32_intersect(&src_rect, &ev->transform.boundingbox,
                              &output->base.region);
    box = pixman_region32_extents(&src_rect);

    sdm_weston_global_transform_rect(ev, box, &sx1, &sy1, &sx2, &sy2);

    pixman_region32_fini(&src_rect);

    src_ret->left = sx1;
    src_ret->top = sy1;
    src_ret->right = sx2;
    src_ret->bottom = sy2;
}

int SdmDisplay::ComputeDirtyRegion(struct weston_view *ev,
                    struct RectArray *dirty) {
    pixman_region32_t temp;
    struct weston_surface *es = ev->surface;
    pixman_box32_t *rectangles = NULL;
    int n, i;

    pixman_region32_init(&temp);
    pixman_region32_intersect_rect(&temp, &es->damage, 0,
                    0, es->width, es->height);
    rectangles = pixman_region32_rectangles(&temp, &n);
    if (!n) {
     pixman_region32_fini(&temp);
     return 0;
    }

    dirty->rects = reinterpret_cast<struct Rect *> \
                    (zalloc(n * sizeof(struct Rect)));
    if (dirty->rects == NULL) {
     DLOGE("out of memory for allocating dirty region\n");
     return -1;
    }
    for (i = 0; i < n; i++) {
     dirty->rects[i].left = (float)rectangles[i].x1;
     dirty->rects[i].top = (float)rectangles[i].y1;
     dirty->rects[i].right = (float)rectangles[i].x2;
     dirty->rects[i].bottom = (float)rectangles[i].y2;
    }
    dirty->count = n;
    pixman_region32_fini(&temp);

    return 0;
}

uint8_t SdmDisplay::GetGlobalAlpha(struct weston_view *ev)
{
    if (ev->alpha > 1.0f)
     return 0xFF;
    else if (ev->alpha < 0.0f)
     return 0;
    else
     return (uint8_t)(0xFF * ev->alpha);
}

int SdmDisplay::GetVisibleRegion(struct drm_output *output, struct weston_view *ev,
                                         pixman_region32_t *aboved_opaque,
                                         struct RectArray *visible)
{
    pixman_region32_t temp;
    pixman_box32_t *rectangles = NULL;
    int n, i;

    pixman_region32_init(&temp);
    pixman_region32_copy(&temp, &ev->transform.boundingbox);
    pixman_region32_subtract(&temp, &temp, aboved_opaque);
    pixman_region32_intersect(&temp, &output->base.region, &temp);
    pixman_region32_translate(&temp, -output->base.x, -output->base.y);

    rectangles = pixman_region32_rectangles(&temp, &n);
    if (!n) {
     pixman_region32_fini(&temp);
     return 0;
    }

    visible->rects = reinterpret_cast<struct Rect *> (zalloc(n * sizeof(struct Rect)));
    if (visible->rects == NULL) {
     DLOGE("out of memory for allocating visible region\n");
     return -1;
    }
    for (i = 0; i < n; i++) {
     visible->rects[i].left = (float)rectangles[i].x1;
     visible->rects[i].top = (float)rectangles[i].y1;
     visible->rects[i].right = (float)rectangles[i].x2;
     visible->rects[i].bottom = (float)rectangles[i].y2;
    }
    visible->count = n;
    pixman_region32_fini(&temp);

    return 0;
}

bool SdmDisplay::IsTransparentGbmFormat(uint32_t format)
{
    return false;
}

const char *SdmDisplay::GetDisplayString() {
    switch (display_type_) {
    case kPrimary:
      return "primary";
    case kHDMI:
      return "hdmi";
    case kVirtual:
      return "virtual";
    default:
      return "invalid";
  }
}

DisplayError SdmDisplay::GetHdrInfo(struct DisplayHdrInfo *display_hdr_info) {
    DisplayError error;

    DisplayConfigFixedInfo fixed_info = {};
    error = display_intf_->GetConfig(&fixed_info);

    if (error != kErrorNone) {
        DLOGE("Failed to get fixed info. Error = %d", error);
        return error;
    }

    hdr_supported_ = fixed_info.hdr_supported;

    if (!fixed_info.hdr_supported) {
        DLOGI("HDR is not supported");
        return error;
    }

    static const float kLuminanceFactor = 10000.0;
    // luminance is expressed in the unit of 0.0001 cd/m2, convert it to 1cd/m2.
    max_luminance_ = FLOAT(fixed_info.max_luminance)/kLuminanceFactor;
    max_average_luminance_ = FLOAT(fixed_info.average_luminance)/kLuminanceFactor;
    min_luminance_ = FLOAT(fixed_info.min_luminance)/kLuminanceFactor;

    display_hdr_info->hdr_supported = fixed_info.hdr_supported;
    display_hdr_info->hdr_eotf = fixed_info.hdr_eotf;
    display_hdr_info->hdr_metadata_type_one = fixed_info.hdr_metadata_type_one;
    display_hdr_info->max_luminance = fixed_info.max_luminance;
    display_hdr_info->average_luminance = fixed_info.average_luminance;
    display_hdr_info->min_luminance = fixed_info.min_luminance;

    return error;
}

SdmNullDisplay::SdmNullDisplay(DisplayType type, CoreInterface *core_intf) {
}

SdmNullDisplay::~SdmNullDisplay() {
}

DisplayError SdmNullDisplay::CreateDisplay(uint32_t display_id) {
  return kErrorNone;
}
DisplayError SdmNullDisplay::DestroyDisplay() {
  return kErrorNone;
}
DisplayError SdmNullDisplay::Prepare(struct drm_output *output) {
  return kErrorNone;
}
DisplayError SdmNullDisplay::Commit(struct drm_output *output) {
  /**
   * TODO: We need to handle releasing the buffer references such that
   * the video buffers/frames keep moving forward in time even though
   * not displayed. This will be done at a later point of time.
   */
  return kErrorNone;
}
DisplayError SdmNullDisplay::SetDisplayState(DisplayState state,
					bool teardown, int *release_fence) {
  return kErrorNone;
}

DisplayError SdmNullDisplay::SetPanelBrightness(float brightness) {
  return kErrorNone;
}

DisplayError SdmNullDisplay::GetPanelBrightness(float *brightness) {
  return kErrorNone;
}

DisplayError SdmNullDisplay::SetVSyncState(bool enable, struct drm_output *output) {
  /**
   * TODO: drm_output_ needs to be re-initialized based on the preferred supported mode
   *       of the plugged-in display. The recent Weston release contains better APIs
   *       to handle this case. Hence this implementation will be improved based upon
   *       the recent Weston release updates.
   */
  drm_output_ = output;
  return kErrorNone;
}

DisplayError SdmNullDisplay::GetDisplayConfiguration(struct DisplayConfigInfo *display_config) {
  uint32_t props_value[3] = {0};
  char null_display_props[MAX_PROP_STR_SIZE] = {0};
  char *prop = NULL, *saveptr = NULL;

  // sdm.null.resolution format is width:height:fps
  SdmDisplayDebugger::Get()->GetProperty(SDM_NULL_DISPLAY_RESOLUTON_PROP_NAME, null_display_props);

  prop = strtok_r(null_display_props, ":", &saveptr);
  for (int i =0; i<3 && prop != NULL; i++)
  {
    props_value[i] = UINT32(atoi(prop));
    prop = strtok_r(NULL, ":", &saveptr);
  }

  if (props_value[0] == 0 || props_value[1] == 0) {
    display_config->x_pixels = SDM_DEAFULT_NULL_DISPLAY_WIDTH;
    display_config->y_pixels = SDM_DEAFULT_NULL_DISPLAY_HEIGHT;
  } else {
    display_config->x_pixels = props_value[0];
    display_config->y_pixels = props_value[1];
  }

  if (props_value[2] == 0)
    display_config->fps = SDM_DEAFULT_NULL_DISPLAY_FPS;
  else
    display_config->fps = props_value[2];

  display_config->x_dpi = SDM_DEAFULT_NULL_DISPLAY_X_DPI;
  display_config->y_dpi = SDM_DEAFULT_NULL_DISPLAY_Y_DPI;
  display_config->vsync_period_ns = UINT32(1000000000/display_config->fps);
  display_config->is_yuv = SDM_DEAFULT_NULL_DISPLAY_IS_YUV;

  return kErrorNone;
}
DisplayError SdmNullDisplay::RegisterCb(int display_id, vblank_cb_t vbcb) {
  vblank_cb_   = vbcb;

  return kErrorNone;
}

DisplayError SdmNullDisplay::GetHdrInfo(struct DisplayHdrInfo *display_hdr_info) {
  return kErrorNone;
}

SdmDisplayProxy::SdmDisplayProxy(DisplayType type, CoreInterface *core_intf,
                                 SdmDisplayBufferAllocator *buffer_allocator)
  : disp_type_(type), core_intf_(core_intf),
    sdm_disp_(type, core_intf, buffer_allocator), null_disp_(type, core_intf) {

    display_intf_ = &sdm_disp_;
    buffer_allocator_ = buffer_allocator;
    std::thread uevent_thread(UeventThread, this);
    uevent_thread_.swap(uevent_thread);
}

SdmDisplayProxy::~SdmDisplayProxy () {
    uevent_thread_exit_ = true;
    uevent_thread_.detach();
}

int SdmDisplayProxy::HandleHotplug(bool connected) {
  return kErrorNone;
}

void *SdmDisplayProxy::UeventThread(void *context) {
  if (context) {
    return reinterpret_cast<SdmDisplayProxy *>(context)->UeventThreadHandler();
  }

  return NULL;
}

#define HAL_PRIORITY_URGENT_DISPLAY (-8)

void *SdmDisplayProxy::UeventThreadHandler() {
  static char uevent_data[4096];
  int length = 0;
  prctl(PR_SET_NAME, uevent_thread_name_, 0, 0, 0);
  setpriority(PRIO_PROCESS, 0, HAL_PRIORITY_URGENT_DISPLAY);
  if (!uevent_init()) {
    DLOGE("Failed to init uevent");
    pthread_exit(0);
    return NULL;
  }

  DLOGI("SdmDisplayProxy::UeventThreadHandler");

  while (!uevent_thread_exit_) {
    // keep last 2 zeroes to ensure double 0 termination
    length = uevent_next_event(uevent_data, INT32(sizeof(uevent_data)) - 2);
    string s(uevent_data, length);

    if (s.find("name=DSI-1") != string::npos) {
      bool connected = s.find("status=connected") != string::npos;
      DLOGI("DSI is %s !\n", connected ? "connected" : "removed");
      HandleHotplug(connected);
    }
  }
  pthread_exit(0);

  return NULL;
}

}  // namespace sdm
