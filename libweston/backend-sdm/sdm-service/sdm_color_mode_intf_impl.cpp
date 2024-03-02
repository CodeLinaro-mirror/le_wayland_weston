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

#include <utils/constants.h>
#include "display_properties.h"
#include <algorithm>

#include "sdm-service/sdm_display_debugger.h"
#include "sdm-service/sdm_color_mode_intf_impl.h"

#define __CLASS__ "SDMColorModeImpl"

#define DEFAULT_RENDER_INTENT kRenderIntentColorimetric
#define DEFAULT_DYNAMIC_RANGE kSdrType

namespace sdm {

#define MAX_PROP_STR_SIZE 64

static SDMColorModeIntfFactory color_mode_intf_factory_;

SDMColorModeIntfFactory *GetSDMColorModeIntfFactory() { return &color_mode_intf_factory_; }

std::unique_ptr<SDMColorModeIntf> SDMColorModeIntfFactory::GetSDMColorModeIntf(
                    DisplayType display_type, DisplayInterface *display_intf) {
  if (display_type == kBuiltIn) {
    return std::unique_ptr<SDMColorModeIntf>(new SDMColorModeImplStc(display_intf));
  }

  if (display_type == kPluggable) {
    return std::unique_ptr<SDMColorModeIntf>(new SDMColorModeImpl(display_intf));
  }

  return std::unique_ptr<SDMColorModeIntf>();
}

SDMColorModeImpl::SDMColorModeImpl(DisplayInterface *display_intf) : display_intf_(display_intf) {}

DisplayError SDMColorModeImpl::Init() {
  PopulateColorModes();
  return kErrorNone;
}

DisplayError SDMColorModeImpl::DeInit() {
  color_mode_map_.clear();
  return kErrorNone;
}

bool SDMColorModeImpl::IsModeSupported(ColorPrimaries primary, GammaTransfer transfer,
                                        RenderIntent intent) {
  if (color_mode_map_.find(primary) == color_mode_map_.end()) {
    return false;
  }

  if (color_mode_map_[primary].find(transfer) == color_mode_map_[primary].end()) {
    return false;
  }

  if (color_mode_map_[primary][transfer].find(intent) ==
                                      color_mode_map_[primary][transfer].end()) {
    return false;
  }

  return true;
}

DisplayError SDMColorModeImpl::GetValidColorMode(ColorPrimaries primary, GammaTransfer transfer,
                          RenderIntent *intent, DynamicRangeType *dynamic_range) {
  if (color_mode_map_.find(primary) == color_mode_map_.end()) {
    DLOGE("Could not find color primary: %d", primary);
    return kErrorNotSupported;
  }

  if (color_mode_map_[primary].find(transfer) == color_mode_map_[primary].end()) {
    DLOGE("Could not find transfer %d in primary %d", transfer, primary);
    return kErrorNotSupported;
  }

  if (color_mode_map_[primary][transfer].find(*intent) ==
                                      color_mode_map_[primary][transfer].end()) {
    // Fall back to default render intent if current render intent is not supported
    DLOGW("Could not find intent %d, checking for intent %d in primary %d, transfer %d",
                                      *intent, DEFAULT_RENDER_INTENT, primary, transfer);
    *intent = DEFAULT_RENDER_INTENT;

    if (color_mode_map_[primary][transfer].find(*intent) ==
                                      color_mode_map_[primary][transfer].end()) {
      DLOGE("Failed to find intent %d in primary %d, transfer %d",
                                  *intent, primary, transfer);
      return kErrorNotSupported;
    }
  }

  if (color_mode_map_[primary][transfer][*intent].find(*dynamic_range) ==
                              color_mode_map_[primary][transfer][*intent].end()) {
    // Fall back to default dynamic range if current render intent is not supported
    DLOGW("Could not find range %d, checking for range %d in intent %d, primary %d, transfer %d",
                              *dynamic_range, DEFAULT_DYNAMIC_RANGE, *intent, primary, transfer);
    *dynamic_range = DEFAULT_DYNAMIC_RANGE;

    if (color_mode_map_[primary][transfer][*intent].find(*dynamic_range) ==
                            color_mode_map_[primary][transfer][*intent].end()) {
      DLOGE("Failed to find range %d, in intent %d, primary %d, transfer %d",
                                  *dynamic_range, *intent, primary, transfer);
      return kErrorNotSupported;
    }
  }
  return kErrorNone;
}

DisplayError SDMColorModeImpl::SetColorModeWithRenderIntent(ColorMode color_mode,
                                                            bool hdr_present) {
  if (current_color_mode_.gamut == color_mode.gamut &&
      current_color_mode_.gamma == color_mode.gamma &&
      current_color_mode_.intent == color_mode.intent &&
      ((hdr_present && (curr_dynamic_range_ == kHdrType)) ||
      (!hdr_present && (curr_dynamic_range_ == kSdrType)))) {
      return kErrorNone;
  }

  ColorPrimaries primary = color_mode.gamut;
  GammaTransfer transfer = color_mode.gamma;
  RenderIntent intent = color_mode.intent;
  DynamicRangeType dynamic_range = hdr_present ? kHdrType : kSdrType;

  DisplayError error = GetValidColorMode(primary, transfer, &intent, &dynamic_range);
  if (error != kErrorNone) {
    return error;
  }

  std::string mode_string = color_mode_map_[primary][transfer][intent][dynamic_range];
  DLOGI("Applying color mode: %s", mode_string.c_str());

  error = display_intf_->SetColorMode(mode_string);
  if (error != kErrorNone) {
    DLOGE("Failed to apply mode: gamut = %d, gamma = %d, intent = %d, range = %d, name = %s\
      , err %d", primary, transfer, intent, dynamic_range, mode_string.c_str(), error);
    return kErrorNotSupported;
  }

  // Overwrite the intent in case default intent was applied
  color_mode.intent = intent;

  current_color_mode_ = color_mode;
  curr_dynamic_range_ = dynamic_range;
  DLOGV_IF(kTagClient, "Successfully applied mode: gamut = %d, gamma = %d, intent = %d, "
      "range = %d, name = %s", primary, transfer, intent, dynamic_range, mode_string.c_str());
  return kErrorNone;
}

void SDMColorModeImpl::PopulateColorModes() {
  uint32_t color_mode_count = 0;
  // SDM returns modes which have attributes defining mode and rendering intent
  DisplayError error = display_intf_->GetColorModeCount(&color_mode_count);
  if (error != kErrorNone || (color_mode_count == 0)) {
    DLOGW("GetColorModeCount failed, use native color mode");
    color_mode_map_[ColorPrimaries_BT709_5][Transfer_sRGB][kRenderIntentNative][kSdrType] =
                                                        "hal_native_identity";
    return;
  }

  DLOGV_IF(kTagClient, "Color Modes supported count = %d", color_mode_count);

  std::vector<std::string> color_modes(color_mode_count);
  error = display_intf_->GetColorModes(&color_mode_count, &color_modes);
  for (uint32_t i = 0; i < color_mode_count; i++) {
    std::string &mode_string = color_modes.at(i);
    DLOGV_IF(kTagClient, "Color Mode[%d] = %s", i, mode_string.c_str());
    AttrVal attr;
    error = display_intf_->GetColorModeAttr(mode_string, &attr);
    std::string color_gamut = kNative, dynamic_range = kSdr, pic_quality = kStandard, transfer;
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

      if (int_render_intent < 0 || int_render_intent > MAX_EXTENDED_RENDER_INTENT) {
        DLOGW("Invalid render intent %d for mode %s", int_render_intent, mode_string.c_str());
        continue;
      }
      DLOGV_IF(kTagClient,
               "color_gamut : %s, dynamic_range : %s, pic_quality : %s, "
               "render_intent : %d",
               color_gamut.c_str(), dynamic_range.c_str(), pic_quality.c_str(), int_render_intent);

      RenderIntent intent = static_cast<RenderIntent>(int_render_intent + 1);
      if (color_gamut == kNative) {
        color_mode_map_[ColorPrimaries_BT709_5][Transfer_sRGB][kRenderIntentNative][kSdrType]
          = mode_string;
      }

      if (color_gamut == kSrgb && dynamic_range == kSdr) {
        color_mode_map_[ColorPrimaries_BT709_5][Transfer_sRGB][intent][kSdrType] = mode_string;
      }

      if (color_gamut == kDcip3 && dynamic_range == kSdr) {
        color_mode_map_[ColorPrimaries_DCIP3][Transfer_sRGB][intent][kSdrType] = mode_string;
      }
      if (color_gamut == kDcip3 && dynamic_range == kHdr) {
        if (display_intf_->IsSupportSsppTonemap()) {
          color_mode_map_[ColorPrimaries_DCIP3][Transfer_sRGB][intent][kHdrType] = mode_string;
        } else if (pic_quality == kStandard) {
          color_mode_map_[ColorPrimaries_BT2020][Transfer_SMPTE_ST2084][intent][kHdrType]
            = mode_string;
          color_mode_map_[ColorPrimaries_BT2020][Transfer_HLG][intent][kHdrType] = mode_string;
        }
      } else if (color_gamut == kBt2020) {
        if (transfer == kSt2084) {
          color_mode_map_[ColorPrimaries_BT2020][Transfer_SMPTE_ST2084][kRenderIntentColorimetric]\
            [kHdrType] = mode_string;
        } else if (transfer == kHlg) {
          color_mode_map_[ColorPrimaries_BT2020][Transfer_HLG][kRenderIntentColorimetric][kHdrType]
            = mode_string;
        } else if (transfer == kSrgb) {
          color_mode_map_[ColorPrimaries_BT2020][Transfer_sRGB][kRenderIntentColorimetric][kHdrType]
            = mode_string;
        }
      }
    } else {
      // Look at the mode names, if no attributes are found
      if (mode_string.find("hal_native") != std::string::npos) {
        color_mode_map_[ColorPrimaries_BT709_5][Transfer_sRGB][kRenderIntentNative][kSdrType]
          = mode_string;
      }
    }
  }
}

#undef __CLASS__
#define __CLASS__ "SDMColorModeImplStc"

SDMColorModeImplStc::SDMColorModeImplStc(DisplayInterface *display_intf) :
  display_intf_(display_intf) {}

DisplayError SDMColorModeImplStc::Init() {
  DisplayError error = display_intf_->GetStcColorModes(&stc_mode_list_);
  if (error != kErrorNone) {
    DLOGW("Failed to get Stc color modes, error %d", error);
    stc_mode_list_.list.clear();
  } else {
    DLOGI("Stc mode count %zu", stc_mode_list_.list.size());
  }

  PopulateColorModes();
  return kErrorNone;
}

DisplayError SDMColorModeImplStc::DeInit() {
  stc_mode_list_.list.clear();
  color_mode_map_.clear();
  return kErrorNone;
}

DisplayError SDMColorModeImplStc::SetColorTransform(const float *matrix) {
  if (!matrix) {
    DLOGE("Invalid parameters : matrix %pK", matrix);
    return kErrorParameters;
  }
  double color_matrix[kColorTransformMatrixCount] = {0};
  CopyColorTransformMatrix(matrix, color_matrix);

  DisplayError error = display_intf_->SetColorTransform(kColorTransformMatrixCount, color_matrix);
  if (error != kErrorNone) {
    DLOGE("Failed to set Color Transform Matrix");
    error = kErrorNotSupported;
  }
  CopyColorTransformMatrix(matrix, color_matrix_);
  return error;
}

DisplayError SDMColorModeImplStc::RestoreColorTransform() {
  DisplayError error = display_intf_->SetColorTransform(kColorTransformMatrixCount, color_matrix_);
  if (error != kErrorNone) {
    DLOGE("Failed to set Color Transform");
    return kErrorParameters;
  }

  return kErrorNone;
}

void SDMColorModeImplStc::PopulateColorModes() {
  bool allow_tonemap_native = 0;
  char property[MAX_PROP_STR_SIZE] = {0};

  SdmDisplayDebugger::Get()->GetProperty(ALLOW_TONEMAP_NATIVE, property);
  if (!(std::string(property)==std::string("1"))) {
      allow_tonemap_native = true;
  }

  if (!stc_mode_list_.list.size()) {
    snapdragoncolor::ColorMode color_mode = {};
    color_mode.intent = snapdragoncolor::kNative;
    color_mode.gamut = allow_tonemap_native ? ColorPrimaries_BT709_5 : ColorPrimaries_Max;
    color_mode.gamma = allow_tonemap_native ? Transfer_sRGB : Transfer_Max;
    color_mode_map_[ColorPrimaries_BT709_5][Transfer_sRGB][kRenderIntentNative][kSdrType] =
      color_mode;
    DLOGI("No color mode supported, add Native mode");
    return;
  }

  for (uint32_t i = 0; i < stc_mode_list_.list.size(); i++) {
    snapdragoncolor::ColorMode stc_mode = stc_mode_list_.list[i];
    if (stc_mode.intent == snapdragoncolor::kNative) {
      // Setting Max for native mode gamut and gamma
      stc_mode.gamut = allow_tonemap_native ? ColorPrimaries_BT709_5 : ColorPrimaries_Max;
      stc_mode.gamma = allow_tonemap_native ? Transfer_sRGB : Transfer_Max;
      color_mode_map_[ColorPrimaries_BT709_5][Transfer_sRGB][kRenderIntentNative][kSdrType] =
        stc_mode;
      DLOGI("Color mode NATIVE supported");
    } else {
      ColorPrimaries gamut = static_cast<ColorPrimaries>(stc_mode.gamut);
      GammaTransfer gamma = static_cast<GammaTransfer>(stc_mode.gamma);
      RenderIntent intent = static_cast<RenderIntent>(stc_mode.intent);
      DynamicRangeType dynamic_range = kSdrType;
      if (std::find(stc_mode.hw_assets.begin(), stc_mode.hw_assets.end(),
                    snapdragoncolor::kPbHdrBlob) != stc_mode.hw_assets.end()) {
        dynamic_range = kHdrType;
      }

      color_mode_map_[gamut][gamma][intent][dynamic_range] = stc_mode;
      DLOGI("Add into map: gamut %d, gamma %d, render_intent %d, dynamic_range %d", gamut, gamma,
            intent, dynamic_range);
    }
  }
}

bool SDMColorModeImplStc::IsModeSupported(ColorPrimaries primary, GammaTransfer transfer,
                                        RenderIntent intent) {
  if (color_mode_map_.find(primary) == color_mode_map_.end()) {
    return false;
  }

  if (color_mode_map_[primary].find(transfer) == color_mode_map_[primary].end()) {
    return false;
  }

  if (color_mode_map_[primary][transfer].find(intent) ==
                                      color_mode_map_[primary][transfer].end()) {
    return false;
  }

  return true;
}

DisplayError SDMColorModeImplStc::GetValidColorMode(ColorPrimaries primary, GammaTransfer transfer,
                          RenderIntent *intent, DynamicRangeType *dynamic_range) {
  if (color_mode_map_.find(primary) == color_mode_map_.end()) {
    DLOGE("Could not find color primary: %d", primary);
    return kErrorNotSupported;
  }

  if (color_mode_map_[primary].find(transfer) == color_mode_map_[primary].end()) {
    DLOGE("Could not find transfer %d in primary %d", transfer, primary);
    return kErrorNotSupported;
  }

  if (color_mode_map_[primary][transfer].find(*intent) ==
                                      color_mode_map_[primary][transfer].end()) {
    // Fall back to default render intent if current render intent is not supported
    DLOGW("Could not find intent %d, checking for intent %d in primary %d, transfer %d",
                                      *intent, DEFAULT_RENDER_INTENT, primary, transfer);
    *intent = DEFAULT_RENDER_INTENT;

    if (color_mode_map_[primary][transfer].find(*intent) ==
                                      color_mode_map_[primary][transfer].end()) {
      DLOGE("Failed to find intent %d in primary %d, transfer %d",
                                  *intent, primary, transfer);
      return kErrorNotSupported;
    }
  }

  if (color_mode_map_[primary][transfer][*intent].find(*dynamic_range) ==
                              color_mode_map_[primary][transfer][*intent].end()) {
    // Fall back to default dynamic range if current render intent is not supported
    DLOGW("Could not find range %d, checking for range %d in intent %d, primary %d, transfer %d",
                              *dynamic_range, DEFAULT_DYNAMIC_RANGE, *intent, primary, transfer);
    *dynamic_range = DEFAULT_DYNAMIC_RANGE;

    if (color_mode_map_[primary][transfer][*intent].find(*dynamic_range) ==
                            color_mode_map_[primary][transfer][*intent].end()) {
      DLOGE("Failed to find range %d, in intent %d, primary %d, transfer %d",
                                  *dynamic_range, *intent, primary, transfer);
      return kErrorNotSupported;
    }
  }
  return kErrorNone;
}

DisplayError SDMColorModeImplStc::SetColorModeWithRenderIntent(ColorMode color_mode,
                                                                bool hdr_present) {
  if (current_color_mode_.gamut == color_mode.gamut &&
      current_color_mode_.gamma == color_mode.gamma &&
      current_color_mode_.intent == color_mode.intent &&
      ((hdr_present && (curr_dynamic_range_ == kHdrType)) ||
      (!hdr_present && (curr_dynamic_range_ == kSdrType)))) {
    return kErrorNone;
  }

  ColorPrimaries primary = color_mode.gamut;
  GammaTransfer transfer = color_mode.gamma;
  RenderIntent intent = color_mode.intent;
  DynamicRangeType dynamic_range = hdr_present ? kHdrType : kSdrType;

  DisplayError error = GetValidColorMode(primary, transfer, &intent, &dynamic_range);
  if (error != kErrorNone) {
    return error;
  }

  snapdragoncolor::ColorMode stc_mode = color_mode_map_[primary][transfer][intent][dynamic_range];
  DLOGI("Applying Stc mode (gamut %d gamma %d intent %d hw_assets.size %d)",
      stc_mode.gamut, stc_mode.gamma, stc_mode.intent, stc_mode.hw_assets.size());

  error = display_intf_->SetStcColorMode(stc_mode);
  if (error != kErrorNone) {
      DLOGE("Failed to apply Stc color mode: gamut %d gamma %d intent %d err %d",
          stc_mode.gamut, stc_mode.gamma, stc_mode.intent, error);
      return kErrorNotSupported;
  }

  // Overwrite the intent in case default intent was applied
  color_mode.intent = intent;

  current_color_mode_ = color_mode;
  curr_dynamic_range_ = dynamic_range;
  DLOGV_IF(kTagClient, "Successfully applied mode: gamut = %d, gamma = %d, intent = %d, "
      "range = %d", primary, transfer, intent, dynamic_range);
  return kErrorNone;
}

} // namespace sdm