/*
* Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
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
*
* Changes from Qualcomm Innovation Center are provided under the following
* license:
*
* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/
/*
 * Copyright © 2008-2011 Kristian Høgsberg
 * Copyright © 2011 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include "config.h"
#include <errno.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <linux/vt.h>
#include <assert.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <time.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#include <gbm.h>
#include <gbm_priv.h>
#include <libudev.h>
#include "shared/helpers.h"
#include "shared/timespec-util.h"
#include "libinput-seat.h"
#include "launcher-util.h"
#ifdef __cplusplus
extern "C" {
#endif
#include <libweston/libweston.h>
#include <libweston/weston-log.h>
#include "libweston-internal.h"
#include "backend.h"
#include "linux-dmabuf.h"
#include "gbm-buffer-backend.h"
#include "screen-capture.h"
#ifdef __cplusplus
}
#endif

/**
 * A small wrapper to print information into the 'drm-backend' debug scope.
 *
 * The following conventions are used to print variables:
 *
 *  - fixed uint32_t values, including Weston object IDs such as weston_output
 *    IDs, DRM object IDs such as CRTCs or properties, and GBM/DRM formats:
 *      "%lu (0x%lx)" (unsigned long) value, (unsigned long) value
 *
 *  - fixed uint64_t values, such as DRM property values (including object IDs
 *    when used as a value):
 *      "%llu (0x%llx)" (unsigned long long) value, (unsigned long long) value
 *
 *  - non-fixed-width signed int:
 *      "%d" value
 *
 *  - non-fixed-width unsigned int:
 *      "%u (0x%x)" value, value
 *
 *  - non-fixed-width unsigned long:
 *      "%lu (0x%lx)" value, value
 *
 * Either the integer or hexadecimal forms may be omitted if it is known that
 * one representation is not useful (e.g. width/height in hex are rarely what
 * you want).
 *
 * This is to avoid implicit widening or narrowing when we use fixed-size
 * types: uint32_t can be resolved by either unsigned int or unsigned long
 * on a 32-bit system but only unsigned int on a 64-bit system, with uint64_t
 * being unsigned long long on a 32-bit system and unsigned long on a 64-bit
 * system. To avoid confusing side effects, we explicitly cast to the widest
 * possible type and use a matching format specifier.
 */
#define drm_debug(b, ...) \
	weston_log_scope_printf((b)->debug, __VA_ARGS__)

struct drm_backend {
  struct weston_backend base;
  struct weston_compositor *compositor;

  struct udev *udev;
  struct wl_event_source *drm_source;

  struct udev_monitor *udev_monitor;
  struct wl_event_source *udev_drm_source;

  struct {
    int id;
    int fd;
    char *filename;
  } drm;
  int render_fd; /* DRM render node file description */
  struct gbm_device *gbm;
  struct wl_listener session_listener;
  uint32_t format;
  int no_addfb3;
  int use_pixman;
  uint32_t prev_state;
  struct udev_input input;
  int32_t cursor_width;
  int32_t cursor_height;

  //TODO(user): these are not required. Need to remove
  uint32_t min_width, max_width;
  uint32_t min_height, max_height;

  /* Flag to indicate whether sdm service is ready */
  bool sdm_repaint;
  /* Timer to finish full initialization of backend */
  struct wl_event_source *finish_full_init;
  struct wl_event_source *input_init;
  /* Whether skip full initialization when backend is created */
  bool early_boot;
  /* Whether is the first repaint for weston */
  bool first_repaint;

  /* Screen capture data */
  struct screen_capture *screen_cap;

  struct weston_log_scope *debug;
};

struct drm_edid {
  char eisa_id[13];
  char monitor_name[13];
  char pnp_id[5];
  char serial_number[13];
};

struct sdm_layer {
  struct wl_list link; /* drm_output::sdm_layer_list */
  struct weston_view *view;
  struct weston_buffer_reference buffer_ref;
  bool is_cursor;
  bool is_skip;
  struct gbm_bo *bo;
  uint32_t composition_type; /* type: enum SDM_COMPOSITION_XXXXX */
  pixman_region32_t overlap;
};

/*
 * for early stage
 */
struct early_layer {
  struct wl_list link; /* drm_output::early_layer_list */
  struct weston_view *view;
  struct gbm_bo *bo;
  struct weston_buffer_reference buffer_ref;
  uint32_t fb_id;
  uint32_t pipe_id;
  bool yuv_required; /* whether need a yuv pipe*/
  uint32_t z_order;
  int32_t hw_block_id; /* store hw_block_id for early pipes handoff*/
  bool secure; /* whether need secure pipe*/
  int32_t sync_handle;
};

struct drm_output;

struct drm_fb {
  struct drm_output *output;
  uint32_t fb_id, stride, handle, size, ion_fd;
  int fd;
  int is_client_buffer;
  struct weston_buffer_reference buffer_ref;

  /* Used by gbm fbs */
  struct gbm_bo *bo;

  /* Used by dumb fbs */
  void *map;
};

struct drm_output {
  struct weston_output   base;
  int display_id;

  uint32_t view_count;
  uint32_t crtc_id;
  int pipe;
  uint32_t connector_id;
  drmModeCrtcPtr original_crtc;
  struct drm_edid edid;
  drmModePropertyPtr dpms_prop;
  uint32_t format;

  enum dpms_enum dpms;

  int frame_pending;
  int page_flip_pending;
  int disable_pending;
  int destroy_pending;

  struct gbm_surface *surface;
  struct gbm_bo *cursor_bo[2];
   /* TODO(user):   Decide whether to use drm_plane or weston_plane */
   /* TODO(user):   struct drm_plane *primary_plane;                */
   /* TODO(user):   or struct weston_plane *primary_plane;          */

  struct weston_view *cursor_view;
  int current_cursor;
  struct drm_fb *current, *next;
  struct backlight *backlight;

  struct drm_fb *dumb[2];
  pixman_image_t *image[2];
  int current_image;
  pixman_region32_t previous_damage;

  struct vaapi_recorder *recorder;
  struct wl_listener recorder_frame_listener;

  struct wl_list plane_flip_list; /* drm_plane::flip_link */
  struct wl_list sdm_layer_list;  /* sdm_layer::link      */
  struct wl_list commited_layer_list;  /* sdm_layer::link */
  struct wl_list early_layer_list;  /* early_layer::link      */
  struct wl_list commited_early_list;  /* early_layer::link */

  bool early_display_enable; /* whether hw display enabled in early stage */
  void *early_display_intf;

  struct wl_event_source *finish_frame_timer;

  int retire_fence_fd;
  struct wl_event_source *retire_fence_source;

  struct {
    unsigned int frame;
    unsigned int sec;
    unsigned int usec;
  } last_vblank;
  /* Indicate whether allocation of framebuffer is UBWC or not */
  int framebuffer_ubwc;
  /* Indicate whether output is secure */
  bool is_secure;
  /* Indicate whether commit layers or not */
  bool layer_none_commit;
  /* Indicate previous frame whether commit layers or not, record previous
   * layer_none_commit
   */
  bool prev_layer_none_commit;
  /* capture buffer in next frame */
  struct screen_capture_buffer *cap_buffer;
  /* Indicate screen capture by GPU or CWB */
  bool cap_fallback_gpu;
  /* unregister cwb and reset cwb display id */
  void (*flush_cwb)(struct drm_output *output);
};
