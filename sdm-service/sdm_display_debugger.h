/*
* Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
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
*/

#ifndef __SDM_DISPLAY_DEBUGGER_H__
#define __SDM_DISPLAY_DEBUGGER_H__

#include <core/sdm_types.h>
#include <debug_handler.h>
#include <stdio.h>
#include <stdarg.h>

#include <cstdlib>
#include <cassert>
#include <stdint.h>

enum {
  NONE,
  ERROR,
  WARNING,
  INFO,
  DEBUG,
  VERBOSE
};

#define WLOG(method, format, ...) SdmDisplayDebugger::Get()->method(__CLASS__ "::%s: " format, \
                                  __FUNCTION__, ##__VA_ARGS__)

#define WLOGE_IF(format, ...) WLOG(Error, format, ##__VA_ARGS__)
#define WLOGW_IF(format, ...) WLOG(Warning, format, ##__VA_ARGS__)
#define WLOGI_IF(format, ...) WLOG(Info, format, ##__VA_ARGS__)
#define WLOGD_IF(format, ...) WLOG(Debug, format, ##__VA_ARGS__)
#define WLOGV_IF(format, ...) WLOG(Verbose, format, ##__VA_ARGS__)

#define DLOGE(format, ...) WLOGE_IF(format, ##__VA_ARGS__)
#define DLOGD(format, ...) WLOGD_IF(format, ##__VA_ARGS__)
#define DLOGW(format, ...) WLOGW_IF(format, ##__VA_ARGS__)
#define DLOGI(format, ...) WLOGI_IF(format, ##__VA_ARGS__)
#define DLOGV(format, ...) WLOGV_IF(format, ##__VA_ARGS__)

namespace sdm {

using display::DebugHandler;

class SdmDisplayDebugger : public DebugHandler {
public:
  SdmDisplayDebugger();
  static inline SdmDisplayDebugger* Get() { return &debug_handler_; }

  // DebugHandler methods
  virtual void Error(const char *format, ...);
  virtual void Warning(const char *format, ...);
  virtual void Info(const char *format, ...);
  virtual void Debug(const char *format, ...);
  virtual void Verbose(const char *format, ...);
  virtual void BeginTrace(const char *class_name, const char *function_name,
                          const char *custom_string);
  virtual void EndTrace();
  virtual int GetProperty(const char *property_name, int *value);
  virtual int GetProperty(const char *property_name, char *value);

  int debug_level_ = INFO;

private:
  static SdmDisplayDebugger debug_handler_;
};

}  // namespace sdm

#endif  // __SDM_DISPLAY_DEBUGGER_H__
