/*
* Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*    * Redistributions of source code must retain the above copyright
*      notice, this list of conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above
*      copyright notice, this list of conditions and the following
*      disclaimer in the documentation and/or other materials provided
*      with the distribution.
*    * Neither the name of The Linux Foundation nor the names of its
*      contributors may be used to endorse or promote products derived
*      from this software without specific prior written permission.

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
*
* Changes from Qualcomm Innovation Center are provided under the following
* license:
*
* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifndef __SDM_DISPLAY_H__
#define __SDM_DISPLAY_H__

#include <core/core_interface.h>
#include <core/display_interface.h>
#include <core/notifier_interface.h>
#include <debug_handler.h>
#include <utils/debug.h>
#include <utils/constants.h>
#include <utils/formats.h>
#include <stdio.h>
#include <string>
#include <utility>
#include <map>
#include <vector>
#include <iostream>
#include <thread>
#include <mutex>

#include "sdm_display_debugger.h"
#include "sdm_display_interface.h"
#include "sdm_display_buffer_allocator.h"
#include "sdm_display_buffer_sync_handler.h"
#include "sdm_display_socket_handler.h"
#include "compositor-sdm-output.h"
#include "drm_master.h"

namespace sdm {
using namespace drm_utils;

using std::vector;
using std::iterator;
using std::string;
using std::to_string;
using std::map;
using std::pair;
using std::fstream;

enum SdmDisplayIntfType {null_disp, sdm_disp};
typedef std::map<uint32_t, HWDisplayInfo> SdmDisplaysInfo;

class SdmLayerManager {
public:
  Layer *get_layer(struct sdm_layer *layer);
private:
  struct SdmLayer {
    SdmLayerManager *layer_manager_;
    Layer layer_;
    struct wl_listener destroy_listener_;
  };
  static void destroy(struct wl_listener *listener, void *data);
  std::map<struct weston_view*, SdmLayer*> layer_cache_;
  std::mutex lock_;
};

class SdmBufferManager {
public:
  uint32_t GetBufferId(int fd, struct weston_buffer *buffer);
private:
  static void destroy_notify(struct wl_listener *listener, void *data);
  std::map<int, uint32_t> buffer_ids;
  uint32_t buffer_id_seed = 0;
  std::mutex buffer_lock;
private:
  struct SdmBuffer {
    struct wl_listener destroy_listener;
    SdmBufferManager *buffer_manager;
    int fd;
  };
};

class SdmDisplayInterface {
public:
  virtual ~SdmDisplayInterface() {}

  virtual DisplayError CreateDisplay() = 0;
  virtual DisplayError DestroyDisplay() = 0;
  virtual DisplayError Prepare(struct drm_output *output) = 0;
  virtual DisplayError Commit(struct drm_output *output) = 0;
  virtual DisplayError Flush(struct drm_output *output) = 0;
  virtual DisplayError SetDisplayState(DisplayState state) = 0;
  virtual DisplayError SetVSyncState(bool enable, struct drm_output *output) = 0;
  virtual DisplayError GetDisplayConfiguration(struct DisplayConfigInfo *display_config) = 0;
  virtual DisplayError EnablePllUpdate(int32_t enable) = 0;
  virtual DisplayError UpdateDisplayPll(int32_t ppm) = 0;
  virtual DisplayError GetHdrInfo(struct DisplayHdrInfo *display_hdr_info) = 0;
  virtual SdmDisplayIntfType GetDisplayIntfType() = 0;
  virtual void FlushConcurrentWriteback() = 0;

  virtual struct drm_output * GetOutput() = 0;

  static int GetDrmMasterFd();
};

class SdmNullDisplay : public SdmDisplayInterface {
public:
  SdmNullDisplay(int32_t display_id, DisplayType type, CoreInterface *core_intf);
  ~SdmNullDisplay();

  SdmDisplayIntfType GetDisplayIntfType() {
    return null_disp;
  }
  DisplayError CreateDisplay();
  DisplayError DestroyDisplay();
  DisplayError Prepare(struct drm_output *output);
  DisplayError Commit(struct drm_output *output);
  DisplayError Flush(struct drm_output *output);
  DisplayError SetDisplayState(DisplayState state);
  DisplayError SetVSyncState(bool enable, struct drm_output *output);
  DisplayError GetDisplayConfiguration(struct DisplayConfigInfo *display_config);
  DisplayError EnablePllUpdate(int32_t enable);
  DisplayError UpdateDisplayPll(int32_t ppm);
  DisplayError GetHdrInfo(struct DisplayHdrInfo *display_hdr_info);
  void FlushConcurrentWriteback() { };

  struct drm_output * GetOutput() { return NULL; };
};

class SdmDisplay : public SdmDisplayInterface, DisplayEventHandler, SdmDisplayDebugger {
public:
  SdmDisplay(int32_t display_id, DisplayType type, CoreInterface *core_intf);
  ~SdmDisplay();

  SdmDisplayIntfType GetDisplayIntfType() {
    return sdm_disp;
  }

  DisplayError CreateDisplay();
  DisplayError DestroyDisplay();
  DisplayError Prepare(struct drm_output *output);
  DisplayError Commit(struct drm_output *output);
  DisplayError Flush(struct drm_output *output);
  DisplayError SetDisplayState(DisplayState state);
  DisplayError SetVSyncState(bool enable, struct drm_output *output);
  DisplayError GetDisplayConfiguration(struct DisplayConfigInfo *display_config);
  DisplayError EnablePllUpdate(int32_t enable);
  DisplayError UpdateDisplayPll(int32_t ppm);

  DisplayError GetHdrInfo(struct DisplayHdrInfo *display_hdr_info);

  struct drm_output * GetOutput() { return drm_output_; };
  void FlushConcurrentWriteback();

protected:
  virtual DisplayError VSync(const DisplayEventVSync &vsync);
  virtual DisplayError CECMessage(char *message);
  virtual DisplayError HandleEvent(DisplayEvent event);
  virtual DisplayError Refresh();

private:
  static const int kBufferDepth = 2;
  DisplayError FreeLayerGeometry(struct LayerGeometry *glayer);
  DisplayError AllocateMemoryForLayerGeometry(struct drm_output *output,
                                              uint32_t index,
                                              struct LayerGeometry *glayer);
  DisplayError AddGeometryLayerToLayerStack(struct drm_output *output,
                                            uint32_t index,
                                            struct LayerGeometry *glayer, bool is_skip);
  DisplayError PopulateLayerGeometryOnToLayerStack(struct drm_output *output,
                                                   uint32_t index,
                                                   struct LayerGeometry *glayer, bool is_skip);
  DisplayError PopulateOutbufferOnToLayerStack(struct drm_output *output);
  int PrepareNormalLayerGeometry(struct drm_output *output,
                                 struct LayerGeometry **glayer,
                                 struct sdm_layer *sdm_layer);
  int PrepareFbLayerGeometry(struct drm_output *output,
                             struct LayerGeometry **fb_glayer);
  DisplayError PrePrepareLayerStack(struct drm_output *output);
  DisplayError PrePrepare(struct drm_output *output);
  DisplayError PostPrepare(struct drm_output *output);
  DisplayError PreCommit();
  DisplayError PostCommit(int *retire_fence_id);
  LayerBufferFormat GetSDMFormat(uint32_t src_fmt,
                                 struct LayerGeometryFlags flags);
  LayerBlending GetSDMBlending(uint32_t source);
  void DumpInputBuffers(void *compositor_output);
  void DumpOutputBuffer(const BufferInfo& buffer_info,
                        void *base, int fence);
  const char*  GetDisplayString();
  /* support functions */
  const char * FourccToString(uint32_t fourcc);
  uint32_t GetMappedFormatFromGbm(uint32_t fmt);
  bool GetVideoPresenceByFormatFromGbm(uint32_t fmt);
  uint32_t GetMappedFormatFromShm(uint32_t fmt);
  bool NeedConvertGbmFormat(struct weston_view *ev, uint32_t format);
  uint32_t ConvertToOpaqueGbmFormat(uint32_t format);
  void ComputeSrcDstRect(struct drm_output *output, struct weston_view *ev,
                         struct Rect *src_ret, struct Rect *dst_ret);
  int ComputeDirtyRegion(struct weston_view *ev, struct RectArray *dirty);
  uint8_t GetGlobalAlpha(struct weston_view *ev);
  int GetVisibleRegion(struct drm_output *output, struct weston_view *ev,
                       pixman_region32_t *aboved_opaque, struct RectArray *visible);
  bool IsTransparentGbmFormat(uint32_t format);
  void PostConcurrentWriteback(struct drm_output *output);
  CoreInterface *core_intf_ = NULL;
  SdmDisplayBufferAllocator buffer_allocator_;
  SdmDisplayBufferSyncHandler buffer_sync_handler_;
  SdmDisplaySocketHandler socket_handler_;
  DisplayEventHandler *client_event_handler_ = NULL;
  DisplayInterface *display_intf_ = NULL;
  DisplayType display_type_ = kDisplayMax;
  DisplayConfigVariableInfo variable_info_;
  HWDisplayInterfaceInfo hw_disp_info_;
  bool shutdown_pending_ = false;
  LayerStack layer_stack_;
  int32_t display_id_ = -1;
  uint32_t fps_ = 0;
  float max_luminance_ = 0.0;
  float max_average_luminance_ = 0.0;
  float min_luminance_ = 0.0;
  int disable_hdr_handling_ = 0;
  bool hdr_supported_ = false;
  Layer fb_layer_;
  LayerBuffer output_buffer_ = {};
  SdmLayerManager layer_manager_;
  SdmBufferManager buffer_manager_;

  struct drm_output *drm_output_ = NULL;
};

class SdmDisplayProxy {
public:
  SdmDisplayProxy(int32_t display_id, DisplayType type, CoreInterface *core_intf);
  ~SdmDisplayProxy();

  DisplayError CreateDisplay() { return display_intf_->CreateDisplay(); }
  DisplayError DestroyDisplay() { return display_intf_->DestroyDisplay(); }
  DisplayError Prepare(struct drm_output *output) {
    return display_intf_->Prepare(output);
  }
  DisplayError Commit(struct drm_output *output) {
    return display_intf_->Commit(output);
  }
  DisplayError Flush(struct drm_output *output) {
    return display_intf_->Flush(output);
  }
  DisplayError SetDisplayState(DisplayState state) {
    return display_intf_->SetDisplayState(state);
  }
  DisplayError SetVSyncState(bool enable, struct drm_output *output) {
    return display_intf_->SetVSyncState(enable, output);
  }
  DisplayError GetDisplayConfiguration(struct DisplayConfigInfo *display_config) {
    return display_intf_->GetDisplayConfiguration(display_config);
  }
  DisplayError RegisterCbs(int display_id, sdm_cbs_t *cbs) {
    hotplug_cb_ = cbs->hotplug_cb;
    return kErrorNone;
  }
  DisplayError EnablePllUpdate(int32_t enable) {
    return display_intf_->EnablePllUpdate(enable);
  }
  DisplayError UpdateDisplayPll(int32_t ppm) {
    return display_intf_->UpdateDisplayPll(ppm);
  }
  DisplayError GetHdrInfo(struct DisplayHdrInfo *display_hdr_info) {
    return display_intf_->GetHdrInfo(display_hdr_info);
  }

  int HandleHotplug(bool connected);
  void FlushConcurrentWriteback() {
    return display_intf_->FlushConcurrentWriteback();
  }

private:
  // Uevent thread
  static void *UeventThread(void *context);
  void *UeventThreadHandler();

  SdmDisplayInterface *display_intf_;
  int32_t display_id_ = -1;
  DisplayType disp_type_;
  CoreInterface *core_intf_;
  SdmNullDisplay null_disp_;
  SdmDisplay sdm_disp_;
  std::thread uevent_thread_;
  bool uevent_thread_exit_ = false;
  const char *uevent_thread_name_ = "SDM_UeventThread";
  hotplug_cb_t hotplug_cb_;
};

}  // namespace sdm

#endif  // __SDM_DISPLAY_H__
