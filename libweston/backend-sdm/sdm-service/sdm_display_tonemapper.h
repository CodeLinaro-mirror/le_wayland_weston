/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __SDM_DISPLAY_TONEMAPPER_H__
#define __SDM_DISPLAY_TONEMAPPER_H__

#include <fcntl.h>
#include <sys/mman.h>

#define TONEMAP_FORWARD 0
#define TONEMAP_INVERSE 1
#include <core/layer_stack.h>
#include <utils/sys.h>
#include <vector>
#include "sdm_display_buffer_sync_handler.h"
#include "sdm_display_buffer_allocator.h"

#ifdef HAS_HDR_SUPPORT
#include <Tonemapper.h>
class Tonemapper;
#endif

namespace sdm {

struct ToneMapConfig {
  int type = 0;
  ColorPrimaries colorPrimaries = ColorPrimaries_Max;
  GammaTransfer transfer = Transfer_Max;
  LayerBufferFormat format = kFormatRGBA8888;
  bool secure = false;
};

class ToneMapSession {
 public:
  explicit ToneMapSession(SdmDisplayBufferAllocator *buffer_allocator);
  ~ToneMapSession();
  DisplayError AllocateIntermediateBuffers(const Layer *layer);
  void FreeIntermediateBuffers();
  void UpdateBuffer(int acquire_fence, LayerBuffer *buffer);
  void SetReleaseFence(int fd);
  void SetToneMapConfig(Layer *layer);
  bool IsSameToneMapConfig(Layer *layer);

  static const uint8_t kNumIntermediateBuffers = 2;
#ifdef HAS_HDR_SUPPORT
  Tonemapper *gpu_tone_mapper_ = nullptr;
#else
  void *gpu_tone_mapper_ = nullptr;
#endif
  SdmDisplayBufferAllocator *buffer_allocator_ = nullptr;
  ToneMapConfig tone_map_config_ = {};
  uint8_t current_buffer_index_ = 0;
  std::vector<BufferInfo> buffer_info_ = {};
  int release_fence_fd_[kNumIntermediateBuffers] = {-1, -1};
  bool acquired_ = false;
  int layer_index_ = -1;
};

class SdmDisplayToneMapper {
 public:
  explicit SdmDisplayToneMapper(SdmDisplayBufferAllocator *allocator)
                                : buffer_allocator_(allocator) {}
  ~SdmDisplayToneMapper() {}

  DisplayError HandleToneMap(LayerStack *layer_stack);
  bool IsActive() { return !tone_map_sessions_.empty(); }
  void PostCommit(LayerStack *layer_stack);
  void SetFrameDumpConfig(uint32_t count);
  void Terminate();

 private:
  void ToneMap(Layer *layer, ToneMapSession *session);
  DisplayError AcquireToneMapSession(Layer *layer, uint32_t *session_index);
  void DumpToneMapOutput(ToneMapSession *session, int *acquire_fence);

  std::vector<ToneMapSession*> tone_map_sessions_ = {};
  SdmDisplayBufferSyncHandler buffer_sync_handler_ = {};
  SdmDisplayBufferAllocator *buffer_allocator_ = nullptr;
  uint32_t dump_frame_count_ = 0;
  uint32_t dump_frame_index_ = 0;
  uint32_t fb_session_index_ = 0;
};

}  // namespace sdm
#endif  // __SDM_DISPLAY_TONEMAPPER_H__
