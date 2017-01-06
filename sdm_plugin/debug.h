/*
 * Copyright (c) 2016-2017 The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
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


#ifndef __SDM_PLUGIN_DEBUG_H__
#define __SDM_PLUGIN_DEBUG_H__

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

/* SDM plugin debug level */
enum {
	SDM_PLUGIN_DEBUG_LEVEL_ERROR,
	SDM_PLUGIN_DEBUG_LEVEL_WARNING,
	SDM_PLUGIN_DEBUG_LEVEL_INFO,
	SDM_PLUGIN_DEBUG_LEVEL_VERBOSE,
};

static FILE *sdm_plugin_log_file = stderr;
static uint32_t sdm_plugin_debug_priority = SDM_PLUGIN_DEBUG_LEVEL_VERBOSE;

#define SDM_PLUGIN_LOG(level, format, ...) sdm_plugin_log(level, format, ##__VA_ARGS__)

#define SDM_PLUGIN_LOGE(format, ...) SDM_PLUGIN_LOG(SDM_PLUGIN_DEBUG_LEVEL_ERROR, format, ##__VA_ARGS__)
#define SDM_PLUGIN_LOGW(format, ...) SDM_PLUGIN_LOG(SDM_PLUGIN_DEBUG_LEVEL_WARNING, format, ##__VA_ARGS__)
#define SDM_PLUGIN_LOGI(format, ...) SDM_PLUGIN_LOG(SDM_PLUGIN_DEBUG_LEVEL_INFO, format, ##__VA_ARGS__)
#define SDM_PLUGIN_LOGV(format, ...) SDM_PLUGIN_LOG(SDM_PLUGIN_DEBUG_LEVEL_VERBOSE, format, ##__VA_ARGS__)


static inline void sdm_plugin_log(uint32_t level, const char *format, ...) {
	va_list list;

	if (level <= sdm_plugin_debug_priority) {
		va_start(list, format);
		vfprintf(sdm_plugin_log_file, format, list);
		va_end(list);
	}
}

#endif /* __SDM_PLUGIN_DEBUG_H__ */
