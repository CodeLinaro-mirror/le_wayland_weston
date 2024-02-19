/*
* Copyright (c) 2017,2020-2021 The Linux Foundation. All rights reserved.
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
*
* Changes from Qualcomm Innovation Center are provided under the following license:
* Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include <display_properties.h>
#include <properties.h>

#include "sdm-service/sdm_display.h"
#include "sdm-service/sdm_display_connect.h"
#include "sdm-service/uevent.h"

#ifdef __cplusplus
extern "C" {
#endif

#define __CLASS__ "SdmDisplayConnect"
namespace sdm {

#define SDM_DISPLAY_DEBUG 0

enum {
       FAIL,
       SUCCESS
};

CoreInterface *core_intf_ = NULL;
SdmDisplayBufferAllocator *buffer_allocator_;
SdmDisplayBufferSyncHandler buffer_sync_handler_;
SdmDisplaySocketHandler socket_handler_;
HWDisplayInterfaceInfo hw_disp_info_;

CreatedDisplaysInfo displays_;

HWDisplaysInfo hw_displays_info_ = {};
// ordered by output id
SdmDisplaysInfo sdm_displays_info_ = {};

SdmDisplayProxy *GetDisplayFromId(uint32_t display_id) {
    auto it = displays_.find(display_id);
    if (it == displays_.end()) {
        DLOGE("No display available with display_id: %d.", display_id);
        return NULL;
    }
    return it->second;
}

bool IsDisplayCreated(uint32_t display_id) {
    return displays_.find(display_id) != displays_.end();
}

int GetConnectedDisplaysIds(int num_displays, uint32_t *connector_ids) {
    if (!connector_ids) {
        return -1;
    }
    SdmDisplaysInfo::iterator iter = sdm_displays_info_.begin();
    int idx = 0;

    if (num_displays != sdm_displays_info_.size()) {
        DLOGE("Connected display sizes doesn't match.");
        return -1;
    }

    for (iter; iter != sdm_displays_info_.end(); ++iter) {
        connector_ids[idx++] = iter->first;
    }

    return 0;
}

static DisplayError
SetProperty(const char *property_name, const char *value)
{
  if (property_set(property_name, value) == 0) {
    return kErrorNone;
  }
  return kErrorNotSupported;
}

int CreateCore(bool use_pixman)
{
    DisplayError error = kErrorNone;
    if (core_intf_) {
        DLOGW("Core was already created.");
        return kErrorNone;
    }
    buffer_allocator_ = new SdmDisplayBufferAllocator(use_pixman);

    // TODO: Check the requirement for this property
    std::shared_ptr<IPCIntf> ipc_intf = nullptr;

    error = CoreInterface::CreateCore(buffer_allocator_,
                                      &buffer_sync_handler_,
                                      &socket_handler_,
                                      ipc_intf,
                                      &core_intf_);
    if (!core_intf_) {
        DLOGE("function failed. Error = %d", error);
        return error;
    }

    #if SDM_DISPLAY_DEBUG
    DLOGD("successfully created.");
    #endif

    return kErrorNone;
}

int DestroyCore()
{
    DisplayError error = kErrorNone;

    if (!core_intf_) {
        DLOGE("Core was already destroyed => core_intf_ = NULL");
        return kErrorNone;
    }

    auto it = displays_.begin();
    for (it; it != displays_.end(); ++it) {
        uint32_t connector_id = it->first;
        delete displays_[connector_id];
        displays_[connector_id] = NULL;
        displays_.erase(connector_id);
    }

    error = CoreInterface::DestroyCore();
    if (error != kErrorNone) {
        DLOGE("function failed. Error = %d", error);
        return error;
    }
    core_intf_ = NULL;
    delete buffer_allocator_;

    #if SDM_DISPLAY_DEBUG
    DLOGD("Core was destroyed successfully");
    #endif

    return kErrorNone;
}

int GetFirstDisplayType(int *display_id)
{
    DisplayError error = kErrorNone;

    *display_id = -1; /* Initialize with invalid display type */
    if (!core_intf_) {
        DLOGE("function failed as core was not created.");
        return kErrorNotSupported;
    }

    error = core_intf_->GetFirstDisplayInterfaceType(&hw_disp_info_);
    if (error != kErrorNone) {
        DLOGE("function GetFirstDisplayInterfaceType failed: error = %d",
              error);
        return error;
    }
    *display_id = hw_disp_info_.type;

    #if SDM_DISPLAY_DEBUG
    DLOGD("function successful: display id = %d", *display_id);
    #endif

    return kErrorNone;
}

int CreateDisplay(uint32_t display_id)
{
    DisplayError error = kErrorNone;
    HWDisplayInfo hw_display_info;

    if (IsDisplayCreated(display_id)) {
        DLOGE("Display(%d) was already created.", display_id);
        return kErrorNone;
    }

    auto it = hw_displays_info_.find(display_id);
    if (it == hw_displays_info_.end()) {
        DLOGE("No display information available for display_id: %d.", display_id);
        return kErrorParameters;
    }
    hw_display_info = it->second;

    if (core_intf_ == NULL) {
        DLOGE("Core is not created yet.");
        return kErrorNotSupported;
    }

    SetProperty(DISABLE_SINGLE_LM_SPLIT_PROP, "1");

    enum DisplayType display_type = hw_display_info.display_type;

    SdmDisplayProxy *sdm_display = new SdmDisplayProxy(display_type, core_intf_, buffer_allocator_);

    displays_[display_id] = sdm_display;
    error = displays_[display_id]->CreateDisplay(display_id) ;
    if (error != kErrorNone) {
        DLOGE("Failed to create display(%d)", display_id);
        delete displays_[display_id];
        displays_[display_id] = NULL;
        displays_.erase(display_id);
        return error;
    }

    #if SDM_DISPLAY_DEBUG
    DLOGD("Display(%d) created successfully.", display_id);
    #endif

    return kErrorNone;
}

int Prepare(uint32_t display_id, struct drm_output *output)
{
    DisplayError error = kErrorNone;
    SdmDisplayProxy *dpy = GetDisplayFromId(display_id);
    if (!dpy) {
        DLOGE("Failed as Display (%d) not created yet.", display_id);
        return kErrorNotSupported;
    }

    error = dpy->Prepare(output);
    if (error != kErrorNone) {
        DLOGE("function failed with error = %d", error);
        return error;
    }

    #if SDM_DISPLAY_DEBUG
    DLOGD("function successful.");
    #endif

    return kErrorNone;
}

int Commit(uint32_t display_id, struct drm_output *output)
{
    DisplayError error = kErrorNone;
    SdmDisplayProxy *dpy = GetDisplayFromId(display_id);
    if (!dpy) {
        DLOGE("Failed as Display (%d) not created yet.", display_id);
        return kErrorNotSupported;
    }

    error = dpy->Commit(output);
    if (error != kErrorNone) {
        DLOGE("function failed with error = %d", error);
        return error;
    }

    DLOGV("function successful.");

    return kErrorNone;
}

int DestroyDisplay(uint32_t display_id)
{
    DisplayError error = kErrorNone;
    SdmDisplayProxy *dpy = GetDisplayFromId(display_id);
    if (!dpy) {
        DLOGE("Failed as Display (%d) not created yet.", display_id);
        return kErrorNotSupported;
    }

    error = dpy->DestroyDisplay();
    if (error != kErrorNone) {
        DLOGE("function failed with error = %d", error);
        return error;
    }

    delete displays_[display_id];
    displays_[display_id] = NULL;
    displays_.erase(display_id);

    #if SDM_DISPLAY_DEBUG
    DLOGD("function successful.");
    #endif

    return kErrorNone;
}

bool GetDisplayConfiguration(uint32_t display_id, struct DisplayConfigInfo *display_config)
{
    DisplayError error = kErrorNone;
    SdmDisplayProxy *dpy = GetDisplayFromId(display_id);
    if (!dpy) {
        DLOGE("Failed as Display (%d) not created yet.", display_id);
        return kErrorNotSupported;
    }

    error = dpy->GetDisplayConfiguration(display_config);

    if (error != kErrorNone) {
        DLOGE("function failed with error = %d", error);
        return FAIL;
    }

    #if SDM_DISPLAY_DEBUG
    DLOGD("function successful.");
    #endif

    return SUCCESS;
}

bool GetDisplayHdrInfo(uint32_t display_id, struct DisplayHdrInfo *display_hdr_info)
{
    SdmDisplayProxy *dpy = GetDisplayFromId(display_id);
    if (!dpy) {
        DLOGE("Failed as Display (%d) not created yet.", display_id);
        return kErrorNotSupported;
    }

    #if SDM_DISPLAY_DEBUG
    DLOGD("function successful.");
    #endif

    return SUCCESS;
}

int RegisterCbs(uint32_t display_id, sdm_cbs *cbs) {
    DisplayError error = kErrorNone;
    SdmDisplayProxy *dpy = GetDisplayFromId(display_id);
    if (!dpy) {
        DLOGE("Failed as Display (%d) not created yet.", display_id);
        return kErrorNotSupported;
    }

    error = dpy->RegisterCbs(display_id, cbs);

    if (error != kErrorNone) {
        DLOGE("function failed with error = %d", error);
        return error;
    }

    return kErrorNone;
}

int get_drm_master_fd(void) {

    int fd = SdmDisplayInterface::GetDrmMasterFd();

    #if SDM_DISPLAY_DEBUG
    DLOGD("fd is: %d \n", fd);
    #endif

    return fd;
}

int SetDisplayState(uint32_t display_id, int power_mode) {
    DisplayError error = kErrorNone;
    bool teardown;
    shared_ptr<Fence> release_fence;
    sdm::DisplayState disp_state;
    SdmDisplayProxy *dpy = GetDisplayFromId(display_id);
    if (!dpy) {
        DLOGE("Failed as Display (%d) not created yet.", display_id);
        return kErrorNotSupported;
    }

    if (power_mode == WESTON_DPMS_ON) {
        teardown = false;
        disp_state = kStateOn;
    } else {
        teardown = true;
        disp_state = kStateOff;
    }
    /* When WESTON_DPMS_ON == 0, set state ON (kStateOn)     */
    /* for all other power modes, i.e. WESTON_DPMS_STANDBY,  */
    /* WESTON_DPMS_SUSPEND, WESTON_DPMS_OFF turn off display */
    /* set state off (kStateOff)                             */
    error = dpy->SetDisplayState(disp_state, teardown,
                                                  &release_fence);
    if (error != kErrorNone) {
        DLOGE("function failed with error = %d", error);
        return error;
    }

    #if SDM_DISPLAY_DEBUG
    DLOGD("function successful.");
    #endif

    return kErrorNone;
}

int SetVSyncState(uint32_t display_id, bool state, struct drm_output *output)
{
    DisplayError error = kErrorNone;
    SdmDisplayProxy *dpy = GetDisplayFromId(display_id);
    if (!dpy) {
        DLOGE("Failed as Display (%d) not created yet.", display_id);
        return kErrorNotSupported;
    }

    error = dpy->SetVSyncState(state, output);
    if (error != kErrorNone) {
        DLOGE("function failed with error = %d", error);
        return error;
    }

    #if SDM_DISPLAY_DEBUG
    DLOGD("function successful.");
    #endif

    return kErrorNone;
}

int SetPanelBrightness(int display_id, float brightness)
{
    DisplayError error = kErrorNone;
    SdmDisplayProxy *dpy = GetDisplayFromId(display_id);
    if (!dpy) {
        DLOGE("Failed as Display (%d) not created yet.", display_id);
        return kErrorNotSupported;
    }

    return dpy->SetPanelBrightness(brightness);
}

int GetPanelBrightness(int display_id, float *brightness)
{
    DisplayError error = kErrorNone;
    SdmDisplayProxy *dpy = GetDisplayFromId(display_id);
    if (!dpy) {
        DLOGE("Failed as Display (%d) not created yet.", display_id);
        return kErrorNotSupported;
    }
    return dpy->GetPanelBrightness(brightness);
}

uint32_t GetDisplayCount(void) {
  uint32_t count = 0;

  count = sdm_displays_info_.size();

  return count;
}


void HandlePrimaryDisplayInfo() {
  HWDisplaysInfo::iterator iter = hw_displays_info_.begin();
  sdm_displays_info_.clear();

  for (iter; iter != hw_displays_info_.end(); ++iter) {
    if (!iter->second.is_primary)
      continue;
    if (iter->second.display_type == sdm::kVirtual)
      continue;
    if (!iter->second.is_connected)
      continue;

    // only one primary display
    sdm_displays_info_[iter->first] = iter->second;
    break;
  }
}

void HandleExternalDisplayInfos() {
  HWDisplaysInfo::iterator iter = hw_displays_info_.begin();

  for (iter; iter != hw_displays_info_.end(); ++iter) {
    if (iter->second.is_primary)
      continue;

    if (iter->second.display_type == sdm::kVirtual)
      continue;

    if (!iter->second.is_connected)
      continue;

    sdm_displays_info_[iter->first] = iter->second;
  }
}

int GetDisplayInfos(void) {
  DisplayError error = kErrorNone;

  error = core_intf_->GetDisplaysStatus(&hw_displays_info_);
  if (error != kErrorNone) {
    DLOGE("function GetDisplaysStatus failed. Error = %d", error);
    return error;
  }

  // Only create non-virtual display first
  /* primary display*/
  HandlePrimaryDisplayInfo();
  /* pluggable display*/
  HandleExternalDisplayInfos();
  return 0;
}

char *GetConnectorName(uint32_t display_id) {
  char name[100]={};
  const char *type_name = NULL;
  auto iter = sdm_displays_info_.find(display_id);

  switch(iter->second.display_type) {
    case kBuiltIn:
      if (iter->second.is_primary) {
        type_name = "DSI-1";
      } else {
        type_name = "DSI-2";
      }
      break;
    case kPluggable:
      type_name = "DP";
      break;
    default:
      type_name = "unKnown";
      break;
  }

  snprintf(name, sizeof name, "%s-%d", type_name, display_id);

  return strdup(name);
}

uint32_t GetConnectorId(uint32_t display_id) {
  auto iter = sdm_displays_info_.find(display_id);
  if (iter == sdm_displays_info_.end()) {
    return -1;
  }
  return iter->second.display_id;
}

static HWDisplayInfo GetSdmDisplayInfo(int display_id) {
  auto iter = sdm_displays_info_.find(display_id);

  return iter->second;
}

int GetDisplayType(uint32_t display_id) {

  auto it = hw_displays_info_.find(display_id);
  if (it == hw_displays_info_.end()) {
    DLOGE("No display information available for display_id: %d.", display_id);
    return -1;
  }

  return (it->second.display_type);
}


}// namespace sdm
#ifdef __cplusplus
}
#endif
