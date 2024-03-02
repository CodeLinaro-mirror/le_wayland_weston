/*
 * Copyright (c) 2014-2021, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Copyright 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Changes from Qualcomm Innovation Center, Inc are provided under the following license:
 *
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __SDM_COLOR_MODE_INTF_IMPL_H__
#define __SDM_COLOR_MODE_INTF_IMPL_H__

#include <private/color_params.h>
#include <core/display_interface.h>
#include <color_metadata.h>
#include <map>

#include "sdm-service/sdm_color_mode_intf.h"

namespace sdm {

/* Color Mode implementation class for external/pluggable displays */
class SDMColorModeImpl : public SDMColorModeIntf {
 public:
  ~SDMColorModeImpl() {}
  explicit SDMColorModeImpl(DisplayInterface *display_intf);
  DisplayError Init();
  DisplayError DeInit();
  bool IsModeSupported(ColorPrimaries primary, GammaTransfer transfer,
                                          RenderIntent intent);
  DisplayError SetColorModeWithRenderIntent(ColorMode mode, bool hdr_present);

  //TBD: Set Color Transform is not supported on External display
  DisplayError SetColorTransform(const float *matrix) { return kErrorNone; }

 private:
  void PopulateColorModes();
  DisplayError GetValidColorMode(ColorPrimaries primary, GammaTransfer transfer,
                          RenderIntent *intent, DynamicRangeType *dynamic_range);


  DisplayInterface *display_intf_ = NULL;
  ColorMode current_color_mode_ = {};
  DynamicRangeType curr_dynamic_range_ = kSdrType;

  typedef std::map<DynamicRangeType, std::string> DynamicRangeMap;
  typedef std::map<RenderIntent, DynamicRangeMap> RenderIntentMap;
  typedef std::map<GammaTransfer, RenderIntentMap> TransferMap;
  std::map<ColorPrimaries, TransferMap> color_mode_map_ = {};
};

/* Color Mode implementation class for internal/builtin displays */
class SDMColorModeImplStc : public SDMColorModeIntf {
 public:
  ~SDMColorModeImplStc() {}
  explicit SDMColorModeImplStc(DisplayInterface *display_intf);
  DisplayError Init();
  DisplayError DeInit();
  DisplayError SetColorModeWithRenderIntent(ColorMode mode, bool hdr_present);
  bool IsModeSupported(ColorPrimaries primary, GammaTransfer transfer,
                                          RenderIntent intent);
  DisplayError SetColorTransform(const float *matrix);

 private:
  template <class T>
  void CopyColorTransformMatrix(const T *input_matrix, double *output_matrix) {
    for (uint32_t i = 0; i < kColorTransformMatrixCount; i++) {
      output_matrix[i] = static_cast<double>(input_matrix[i]);
    }
  }
  DisplayError RestoreColorTransform();
  void PopulateColorModes();
  DisplayError GetValidColorMode(ColorPrimaries primary, GammaTransfer transfer,
                          RenderIntent *intent, DynamicRangeType *dynamic_range);

  static const uint32_t kColorTransformMatrixCount = 16;
  double color_matrix_[kColorTransformMatrixCount] = {1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0,
                                                      0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0};
  DisplayInterface *display_intf_ = NULL;
  ColorMode current_color_mode_ = {};
  DynamicRangeType curr_dynamic_range_ = kSdrType;
  snapdragoncolor::ColorModeList stc_mode_list_;
  typedef std::map<DynamicRangeType, snapdragoncolor::ColorMode> DynamicRangeMap;
  typedef std::map<RenderIntent, DynamicRangeMap> RenderIntentMap;
  typedef std::map<GammaTransfer, RenderIntentMap> TransferMap;
  std::map<ColorPrimaries, TransferMap> color_mode_map_ = {};
};

}  // namespace sdm

#endif  // __SDM_COLOR_MODE_INTF_IMPL_H__