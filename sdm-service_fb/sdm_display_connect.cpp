/*
* Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
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

#include "sdm_display.h"
#include "sdm_display_connect.h"

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
SdmDisplay *display_[kDisplayMax] = {0};

int CreateCore()
{
  DisplayError error = kErrorNone;
    if (core_intf_) {
        DLOGW("Core was already created.");
        return kErrorNone;
    }
    buffer_allocator_ = new SdmDisplayBufferAllocator;

    error = CoreInterface::CreateCore(SdmDisplayDebugger::Get(),
                                      buffer_allocator_,
                                      &buffer_sync_handler_,
                                      &socket_handler_,
                                      &core_intf_);
    if (!core_intf_) {
        DLOGE("function failed. Error = %d", error);
        return error;
    }

#if SDM_DISPLAY_DEBUG
    DLOGI("successfully created.");
#endif

    return kErrorNone;
}

int DestroyCore()
{
    if (!core_intf_) {
        DLOGE("Core was already destroyed => core_intf_ = NULL");
        return kErrorNone;
    }

    core_intf_->DestroyCore();
    core_intf_ = NULL;
    delete buffer_allocator_;

#if SDM_DISPLAY_DEBUG
    DLOGI("Core was destroyed successfully");
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
    DLOGI("function successful: display id = %d", *display_id);
#endif

    return kErrorNone;
}

int CreateDisplay(int display_id)
{
   DisplayError error = kErrorNone;
    if (display_id >= kDisplayMax || display_id < 0) {
        DLOGE("Display id(%d) out of range.", display_id);
        return kErrorParameters;
    }
    if (display_[display_id] != NULL) {
        DLOGE("Display(%d) was already created.", display_id);
        return kErrorNone;
    }
    if (core_intf_ == NULL) {
        DLOGE("Core is not created yet.");
        return kErrorNotSupported;
    }
    enum DisplayType display_type;
    switch(display_id) {
       case 0:  display_type  = kPrimary;    break;
       case 1:  display_type  = kHDMI;       break;
       case 2:  display_type  = kVirtual;    break;
       default: display_type  = kDisplayMax; break;
    }
    display_[display_id] = new SdmDisplay(display_type, core_intf_, buffer_allocator_);

    error = display_[display_id]->CreateDisplay();
    if (error != kErrorNone) {
        DLOGE("Failed to create display(%d)", display_id);
        delete display_[display_id];
        display_[display_id] = NULL;

        return error;
    }

    DLOGI("Display(%d) created successfully.", display_id);

    return kErrorNone;
}


int Commit(int display_id, int fd)
{
  DisplayError error = kErrorNone;

    if (display_id >= kDisplayMax || display_id < 0) {
        DLOGE("Display id(%d) out of range.", display_id);
        return kErrorParameters;
    }

    if (!display_[display_id]) {
        DLOGE("function failed as Display(%d) not created yet.",
              display_id);
        return kErrorNotSupported;
    }

    error = display_[display_id]->Commit(fd);
    if (error != kErrorNone) {
        DLOGE("function failed with error = %d", error);
        return error;
    }

    #if SDM_DISPLAY_DEBUG
    DLOGD("function successful.");
    #endif

    return kErrorNone;
}

int DestroyDisplay(int display_id)
{
    if (!display_[display_id]) {
        DLOGW("Display(%d) was already destroyed.", display_id);
        return kErrorNone;
    }

    display_[display_id]->DestroyDisplay();
    delete display_[display_id];
    display_[display_id] = NULL;

#if SDM_DISPLAY_DEBUG
    DLOGI("function successful.");
#endif

    return kErrorNone;
}

void SetLineLength(int display_id, int line_length)
{
    if (!display_[display_id]) {
        DLOGW("Display not create when set len.");
        return;
    }

    display_[display_id]->SetFBTStride(line_length);
#if SDM_DISPLAY_DEBUG
    DLOGI("function successful.");
#endif
}

int SetDisplayState(int display_id, int power_mode) {
    DisplayError error = kErrorNone;
    DisplayState state = kStateOff;

    if (!display_[display_id]) {
        DLOGW("Display not create when set state.");
        return kErrorParameters;
    }

    if (power_mode == kDisplayStateOn) {
        state = kStateOn;
    }
    error = display_[display_id]->SetDisplayState(state);
    if (error != kErrorNone) {
        DLOGE("function failed with error = %d", error);
        return error;
    }
#if SDM_DISPLAY_DEBUG
    DLOGI("function successful.");
#endif
    return kErrorNone;
}

void SetVSyncState(int display_id, bool state)
{
    if (!display_[display_id]) {
        DLOGW("Display not create when set vsync.");
        return;
    }

    display_[display_id]->SetVSyncState(state);

#if SDM_DISPLAY_DEBUG
    DLOGI("function successful.");
#endif
}

void RegisterVSyncCb(int display_id, vsync_cb_t vsync_cb)
{
    if (!display_[display_id]) {
        DLOGW("Display not create when set cb.");
        return;
    }

    display_[display_id]->RegisterVSyncCb(vsync_cb);
#if SDM_DISPLAY_DEBUG
    DLOGI("function successful.");
#endif
}

void SetBrightness(int display_id, int brightness)
{
    if (!display_[display_id]) {
        DLOGW("Display not create when set bl.");
        return;
    }

    display_[display_id]->SetBrightness(brightness);
#if SDM_DISPLAY_DEBUG
    DLOGI("function successful.");
#endif
}

void GetBrightness(int display_id, int* brightness)
{
    if (!display_[display_id]) {
        DLOGW("Display not create when get bl.");
        return;
    }

    display_[display_id]->GetBrightness(brightness);
#if SDM_DISPLAY_DEBUG
    DLOGI("function successful.");
#endif
}


}// namespace sdm
#ifdef __cplusplus
}
#endif
