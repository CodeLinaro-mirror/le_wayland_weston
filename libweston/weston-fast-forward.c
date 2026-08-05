/*
 * Copyright (C) 2026 Amazon.com, Inc. or its affiliates
 * Copyright 2025, Collabora, Ltd.
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

#include <libweston/libweston.h>
#include <libweston/weston-fast-forward.h>
#include "libweston-internal.h"
#include "shared/helpers.h"
#include "shared/xalloc.h"
#include "weston-trace.h"

struct weston_fast_forward {
	struct weston_surface *surface;
	struct wl_listener surface_destroy_listener;
};

static void
ffwd_destructor(struct wl_resource *resource)
{
	struct weston_fast_forward *ffwd = wl_resource_get_user_data(resource);

	if (ffwd->surface) {
		wl_list_remove(&ffwd->surface_destroy_listener.link);
		ffwd->surface->fast_forwarder = NULL;
	}

	free(ffwd);
}

static void
ffwd_fast_forward(struct wl_client *client, struct wl_resource *resource)
{
	struct weston_fast_forward *ffwd = wl_resource_get_user_data(resource);
	struct weston_surface *surface = ffwd->surface;

	if (!surface) {
		wl_resource_post_error(resource,
				       WESTON_FAST_FORWARD_V1_ERROR_SURFACE_DESTROYED,
				       "surface destroyed");
		return;
	}
	WESTON_TRACE_FUNC(("fast forward queued", &surface->pending.flow));

	surface->pending.fast_forward = true;
}

static void
ffwd_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct weston_fast_forward_v1_interface weston_ffwd_interface = {
	.destroy = ffwd_destroy,
	.fast_forward = ffwd_fast_forward,
};

static void
ffwd_manager_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
ffwd_surface_destroy_cb(struct wl_listener *listener, void *data)
{
	struct weston_fast_forward *ffwd =
		container_of(listener,
			struct weston_fast_forward, surface_destroy_listener);

	ffwd->surface = NULL;
}

static void
ffwd_manager_get_ffwd(struct wl_client *client,
		      struct wl_resource *ffwdm_resource,
		      uint32_t id,
		      struct wl_resource *surface_resource)
{
	struct weston_fast_forward *ffwd;
	struct weston_surface *surface = wl_resource_get_user_data(surface_resource);
	struct wl_resource *res;

	if (surface->fast_forwarder) {
		wl_resource_post_error(ffwdm_resource,
				       WESTON_FAST_FORWARD_MANAGER_V1_ERROR_ALREADY_EXISTS,
				       "Fast forward resource already exists on surface");
		return;
	}

	res = wl_resource_create(client, &weston_fast_forward_v1_interface,
				 wl_resource_get_version(ffwdm_resource), id);
	if (!res) {
		wl_resource_post_no_memory(ffwdm_resource);
		return;
	}

	ffwd = xzalloc(sizeof *ffwd);
	ffwd->surface = surface;
	ffwd->surface_destroy_listener.notify = ffwd_surface_destroy_cb;
	wl_signal_add(&surface->destroy_signal, &ffwd->surface_destroy_listener);
	wl_resource_set_implementation(res, &weston_ffwd_interface, ffwd,
				       ffwd_destructor);
	surface->fast_forwarder = ffwd;
}

static const struct weston_fast_forward_manager_v1_interface weston_ffwdm_interface = {
	.destroy = ffwd_manager_destroy,
	.get_fast_forward = ffwd_manager_get_ffwd,
};

static void
bind_ffwd_manager(struct wl_client *client,
		  void *data,
		  uint32_t version,
		  uint32_t id)
{
	struct wl_resource *resource;
	struct weston_compositor *compositor = data;

	resource = wl_resource_create(client,
				      &weston_fast_forward_manager_v1_interface,
				      version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource,
				       &weston_ffwdm_interface,
				       compositor, NULL);
}

/** Advertise weston-fast-forward protocol support
 *
 * Sets up weston-fast-forward support so it is advertiszed to clients.
 *
 * \param compositor The compositor to init for.
 * \return Zero on success, -1 on failure.
 */
int
weston_fast_forward_setup(struct weston_compositor *compositor)
{
        if (!wl_global_create(compositor->wl_display,
                              &weston_fast_forward_manager_v1_interface,
                              1, compositor,
                              bind_ffwd_manager))
                return -1;

        return 0;
}
