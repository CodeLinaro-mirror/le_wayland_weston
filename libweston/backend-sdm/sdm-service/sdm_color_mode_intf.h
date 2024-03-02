/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __SDM_COLOR_MODE_INTF_H__
#define __SDM_COLOR_MODE_INTF_H__

#include <color_metadata.h>

namespace sdm {

class SDMColorModeIntf {
 public:
  virtual ~SDMColorModeIntf() {}
  virtual DisplayError Init() = 0;
  virtual DisplayError DeInit() = 0;
  virtual bool IsModeSupported(ColorPrimaries primary, GammaTransfer transfer,
                                          RenderIntent intent) = 0;
  virtual DisplayError SetColorModeWithRenderIntent(ColorMode mode, bool hdr_present) = 0;
  virtual DisplayError SetColorTransform(const float *matrix) = 0;
};

class SDMColorModeIntfFactory {
 public:
  std::unique_ptr<SDMColorModeIntf> GetSDMColorModeIntf(DisplayType display_type,
                                                DisplayInterface *display_intf);
};

extern "C" SDMColorModeIntfFactory *GetSDMColorModeIntfFactory();

}  // namespace sdm

#endif  // __SDM_COLOR_MODE_INTF_H__