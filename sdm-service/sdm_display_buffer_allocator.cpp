/*
* Copyright (c) 2017, The Linux Foundation. All rights reserved.
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

#include "sdm_display_debugger.h"
#include "sdm_display_buffer_allocator.h"
#include "sdm_display_connect.h"
#define __CLASS__ "SdmDisplayBufferAllocator"

#ifdef __cplusplus
extern "C" {
#endif
namespace sdm {

SdmDisplayBufferAllocator::SdmDisplayBufferAllocator() {
    int drm_fd = get_drm_master_fd();

    gbm_ = gbm_create_device(drm_fd);

}

DisplayError SdmDisplayBufferAllocator::AllocateBuffer(BufferInfo *buffer_info) {
  DLOGW("Not supported.");

  return kErrorNone;
}

DisplayError SdmDisplayBufferAllocator::FreeBuffer(BufferInfo *buffer_info) {
  DLOGW("Not supported.");

  return kErrorNone;
}

uint32_t SdmDisplayBufferAllocator::GetBufferSize(BufferInfo *buffer_info) {
  DLOGW("Not supported.");

  return 0;
}

int SdmDisplayBufferAllocator::SetBufferInfo(LayerBufferFormat format,
                                             int *target, int *flags) {
  switch (format) {
  case kFormatRGBA8888:                 *target = GBM_FORMAT_ABGR8888;             break;
  case kFormatRGBX8888:                 *target = GBM_FORMAT_XBGR8888;             break;
  case kFormatRGB888:                   *target = GBM_FORMAT_BGR888;               break;
  case kFormatRGB565:                   *target = GBM_FORMAT_BGR565;               break;
  case kFormatBGR565:                   *target = GBM_FORMAT_RGB565;               break;
  case kFormatBGRA8888:                 *target = GBM_FORMAT_ARGB8888;             break;
  case kFormatBGRX8888:                 *target = GBM_FORMAT_XRGB8888;             break;
  case kFormatYCbCr420SemiPlanarVenus:  *target = GBM_FORMAT_NV12;                 break;
  case kFormatYCbCr420TP10Ubwc:         *target = GBM_FORMAT_YCbCr_420_TP10_UBWC;  break;
  case kFormatYCbCr420P010Ubwc:         *target = GBM_FORMAT_YCbCr_420_P010_UBWC;  break;
  default:
    DLOGE("Unsupported format = 0x%x", format);
    return -1;
  }

  return 0;
}

DisplayError SdmDisplayBufferAllocator::GetAllocatedBufferInfo(const BufferConfig \
                                                               &buffer_config,
                                                               AllocatedBufferInfo \
                                                               *allocated_buffer_info) {
  DLOGW("Not supported.");

  return kErrorNone;
}

DisplayError SdmDisplayBufferAllocator::GetBufferLayout(const AllocatedBufferInfo &buf_info,
                                                 uint32_t stride[4], uint32_t offset[4],
                                                 uint32_t *num_planes) {
    struct gbm_bo *bo;
    struct gbm_import_fd_data import_fd_data;
    int format1 = GBM_FORMAT_ARGB8888;
    int flags = 0;
    generic_buf_layout_t buf_layout;

    SetBufferInfo(buf_info.format, &format1, &flags);

    import_fd_data.fd = buf_info.fd;
    import_fd_data.format = format1;
    import_fd_data.width = buf_info.aligned_width;
    import_fd_data.height = buf_info.aligned_height;

    // Import gbm bo from buf_info
    bo = gbm_bo_import(gbm_, GBM_BO_IMPORT_FD, &import_fd_data, GBM_BO_USE_SCANOUT);

    if (bo == NULL) {
        return kErrorNone;
    }

    uint32_t width, height;
    uint32_t *fbid;
    uint32_t fb_id, stride1, handle, size, format;
    uint32_t fb_id1;
    width = gbm_bo_get_width(bo);
    height = gbm_bo_get_height(bo);
    stride1 = gbm_bo_get_stride(bo);
    handle = gbm_bo_get_handle(bo).u32;
    format = gbm_bo_get_format(bo);


    //check for RGB format
    if (format != GBM_FORMAT_NV12) {
      stride[0] = gbm_bo_get_stride(bo);
      offset[0] = 0;
      *num_planes++;
      return kErrorNone;
    }

    // for NV12 format
    *num_planes = 2;
    gbm_perform(GBM_PERFORM_GET_PLANE_INFO, bo, &buf_layout);

    stride[0] = gbm_bo_get_stride(bo);
    offset[0] = 0;

    stride[1] = stride[0];//buf_layout.planes[1].v_increment;
    offset[1] = stride[0]*height;

  return kErrorNone;
}

}  // namespace sdm
#ifdef __cplusplus
}
#endif
