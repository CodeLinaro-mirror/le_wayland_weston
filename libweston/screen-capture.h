/*
*    Copyright (c) 2018-2019, The Linux Foundation. All rights reserved.
*
*    Redistribution and use in source and binary forms, with or without
*    modification, are permitted provided that the following conditions are
*    met:
*    * Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above
*    copyright notice, this list of conditions and the following
*    disclaimer in the documentation and/or other materials provided
*    with the distribution.
*    * Neither the name of The Linux Foundation nor the names of its
*    contributors may be used to endorse or promote products derived
*    from this software without specific prior written permission.

*    THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
*    WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
*    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
*    ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
*    BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
*    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
*    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
*    BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
*    WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
*    OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
*    IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
*    Changes from Qualcomm Innovation Center are provided under the following
*    license:
*
*    Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
*    SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifndef WESTON_SCREEN_CAPTURE_H
#define WESTON_SCREEN_CAPTURE_H

#include <libweston/libweston.h>

#define SC_LOG_FATAL    (0)
#define SC_LOG_ERR      (1)
#define SC_LOG_WARN     (2)
#define SC_LOG_INFO     (3)
#define SC_LOG_DBG      (4)
#define SC_MAX_DBG_LEVEL  SC_LOG_INFO

#define SC_FATAL_STRING  "SC_FATAL::"
#define SC_ERR_STRING    "SC_ERR::"
#define SC_WARN_STRING   "SC_WARN::"
#define SC_INFO_STRING   "SC_INFO::"
#define SC_DBG_STRING    "SC_DBG::"

#define SC_PROTOCOL_LOG(level, ...) do {  \
										if ((level) <= SC_MAX_DBG_LEVEL) { \
											char *prefix = NULL; \
											if(level==SC_LOG_FATAL) \
												prefix = SC_FATAL_STRING; \
											if(level==SC_LOG_ERR) \
												prefix = SC_ERR_STRING; \
											if(level==SC_LOG_WARN) \
												prefix = SC_WARN_STRING; \
											if(level==SC_LOG_INFO) \
												prefix = SC_INFO_STRING; \
											if(level==SC_LOG_DBG) \
												prefix = SC_DBG_STRING; \
											weston_log("%s%s(%d)::%s", prefix, __func__, __LINE__, __VA_ARGS__); \
										} \
									} while (0)


struct screen_capture {
	uint32_t width;
	uint32_t height;
	struct weston_compositor *compositor;
	uint32_t mirror_output_id;
	void *virtual_output; /* point to drm_output to avoid nested definition */
	struct drm_output *main_output;
	bool enabled;
	bool fallback_gpu;
	bool force_gpu;
	bool destroy_pending;
	bool output_destroy_pending;
	struct weston_view *view; /* record the view which owns the capture buffer */

	/* buffers in the list is pending on capture */
	struct wl_list attached_buf_list;

	/* buffers which had been captured done, if client frame/attach these buffers again,
	 * they could be moved to the attached list
	 */
	struct wl_list free_buf_list;

	struct weston_buffer_reference buf_ref;
	struct screen_capture_buffer *current;
	struct screen_capture_buffer *next;
	struct wl_resource *resource;
	struct wl_event_source *timeout_source;
};

struct screen_capture_buffer {
	struct weston_buffer *buffer;
	/* release fence fd to be signaled by kernel when WB finished */
	int release_fence_fd;
	int buffer_fd;
	int format;
	int type;
	struct gbm_bo *bo;
	bool is_secure;
	bool is_ubwc;
	struct wl_list link;
	struct weston_buffer_reference buf_ref;
	struct wl_event_source *cap_fence_source;
};

/** Advertise screen capture support
 *
 * Calling this initializes the screen capture protocol support, so that
 * the interface will be advertised to clients. Essentially it creates a
 * global. Do not call this function multiple times in the compositor's
 * lifetime. There is no way to deinit explicitly, globals will be reaped
 * when the wl_display gets destroyed.
 *
 * \param compositor The compositor to init for.
 * \return Zero on success, -1 on failure.
 */
int screen_capture_setup(struct weston_compositor *compositor);

/** Ensure if it is screen capture buffer or not
*
*\param buffer to judge whether this buffer is from screen capture client.
*\return true on scree capture buffer,  otherwise false .
*/
bool is_screen_capture_buffer(struct weston_buffer *buffer);

/** Ensure if it is screen capture view or not
*
*\param ev Get the capture view state from ev.
*\return true on scree capture view,  otherwise false .
*/
bool is_screen_capture_view(struct weston_view *ev);

void screen_capture_attach(struct weston_compositor *compositor,
																		struct weston_buffer *buffer);

#endif /* WESTON_SCREEN_CAPTURE_H */
