/*
* Copyright (c) 2021, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*  * Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
*  * Redistributions in binary form must reproduce the above
*    copyright notice, this list of conditions and the following
*    disclaimer in the documentation and/or other materials provided
*    with the distribution.
*  * Neither the name of The Linux Foundation nor the names of its
*    contributors may be used to endorse or promote products derived
*    from this software without specific prior written permission.
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
*
* Changes from Qualcomm Innovation Center are provided under the following license:
*
* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <libweston/libweston.h>

#include "weston-qti-extn-server-protocol.h"
#include "libweston-internal.h"
#include "weston-qti-extn.h"

const struct weston_qti_extn_interface weston_qti_extn_impl = {
  destroy,
  power_on,
  power_off,
  set_brightness
};

void power_on(struct wl_client *client, struct wl_resource *resource) {
  struct weston_compositor *compositor;
  compositor = wl_resource_get_user_data(resource);
  if (compositor == NULL) {
    weston_log("error: compositor not found\n");
    return;
  }

  weston_compositor_wake(compositor);
}

void power_off(struct wl_client *client, struct wl_resource *resource) {
  struct weston_compositor *compositor;
  compositor = wl_resource_get_user_data(resource);
  if (compositor == NULL) {
    weston_log("error: compositor not found\n");
    return;
  }

  weston_compositor_sleep(compositor);
}

void set_brightness(struct wl_client *client, struct wl_resource *resource,
                    uint32_t brightness_value) {
  struct weston_compositor *compositor;
  compositor = wl_resource_get_user_data(resource);
  if (compositor == NULL) {
    weston_log("error: compositor not found\n");
    return;
  }

  struct weston_output *output;
  wl_list_for_each(output, &compositor->output_list, link) {
    if (output) {
      output->set_backlight(output, brightness_value);
    }
  }
}

void destroy(struct wl_client *client, struct wl_resource *resource) {
  wl_resource_destroy(resource);
}

void
bind_weston_qti_extn(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
  struct weston_compositor *compositor = data;
  struct wl_resource *resource;

  weston_log("bind_weston_qti_extn::Invoked\n");
  resource = wl_resource_create(client, &weston_qti_extn_interface, version, id);
  if (resource == NULL) {
    wl_client_post_no_memory(client);
    return;
  }

  wl_resource_set_implementation(resource, &weston_qti_extn_impl, compositor, NULL);
}

/** Advertise weston_qti_extn_setup support
 *
 * Calling this initializes the weston_qti_extn protocol support, so that
 * the interface will be advertised to clients. Essentially it creates a
 * global. Do not call this function multiple times in the compositor's
 * lifetime. There is no way to deinit explicitly, globals will be reaped
 * when the wl_display gets destroyed.
 *
 * \param compositor The compositor to init for.
 * \return Zero on success, -1 on failure.
 */
WL_EXPORT int weston_qti_extn_setup(struct weston_compositor *compositor) {
  weston_log("weston_qti_extn_setup::Invoked\n");
  if (!wl_global_create(compositor->wl_display, &weston_qti_extn_interface, 1,
                        compositor, bind_weston_qti_extn)) {
    return -1;
  }
  return 0;
}



