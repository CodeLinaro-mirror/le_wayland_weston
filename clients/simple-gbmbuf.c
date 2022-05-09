/*
 * Copyright © 2021 The Linux Foundation. All rights reserved.
 * Copyright © 2011 Benjamin Franzke
 * Copyright © 2010 Intel Corporation
 * Copyright © 2014 Collabora Ltd.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

/*
* Changes from Qualcomm Innovation Center are provided under the following license:
*
* Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted (subject to the limitations in the
* disclaimer below) provided that the following conditions are met:
*
*    * Redistributions of source code must retain the above copyright
*      notice, this list of conditions and the following disclaimer.
*
*    * Redistributions in binary form must reproduce the above
*      copyright notice, this list of conditions and the following
*      disclaimer in the documentation and/or other materials provided
*      with the distribution.
*
*    * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
*      contributors may be used to endorse or promote products derived
*      from this software without specific prior written permission.
*
* NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
* GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
* HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
* IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
* ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
* GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
* IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
* OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include "fullscreen-shell-unstable-v1-client-protocol.h"
#include "gbm-buffer-backend-client-protocol.h"
#include "gbm.h"
#include "gbm_priv.h"

struct display;
struct window;
struct buffer;

#define DRV_DRM 1
#define NUM_GBM_BUFFERS 3

#define COLOR_Y 	(~0x80)
#define COLOR_CBCR	(~0x7E7E)

struct display {
	struct wl_display *display;
	struct window *window;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct xdg_wm_base *wm_base;
	struct zwp_fullscreen_shell_v1 *fshell;
	struct gbm_buffer_backend *gbmbuf;
	int xrgb8888_format_found;
};

struct buffer {
	struct wl_buffer *buffer;
	int busy;

	int drm_fd;
	int fb_fd;

	struct gbm_device *gbm;
	struct gbm_bo *bo;

	uint32_t gem_handle;
	int gbmbuf_fd;
	int gbmbuf_metafd;
	uint8_t *mmap;

	int read_done;
	int width;
	int height;
	unsigned int format;
	unsigned int flags;
	int bpp;
	unsigned int stride;
};

struct window {
	struct display *display;
	int width, height;
	struct wl_surface *surface;
	struct zxdg_surface_v6 *xdg_surface;
	struct zxdg_toplevel_v6 *xdg_toplevel;
	struct buffer buffers[NUM_GBM_BUFFERS];
	struct buffer *prev_buffer;
	struct wl_callback *callback;
	bool wait_for_configure;
};

static int running = 1;

#ifdef DRV_FB
#define DRV_NODE "/dev/fb0"
#elif defined(DRV_DRM)
#define DRV_NODE "/dev/dri/card0"
#endif

static void
memset16(void *p_dst, uint16_t value, int count)
{
	uint16_t *ptr = p_dst;
	while(count--)
		*ptr++ = value;
}

static void
redraw(void *data, struct wl_callback *callback, uint32_t time);

static int
map_bo(struct buffer *my_buf)
{
	int ret = 0;
	ret = gbm_perform(GBM_PERFORM_CPU_MAP_FOR_BO, my_buf->bo, &my_buf->mmap);
	if (ret != GBM_ERROR_NONE) {
		printf("gbm bo map failed with ret = %d\n", ret);
		return 0;
	}
	return 1;
}

static void
fill_nv12_content(struct buffer *buf, uint8_t y, uint16_t cbcr)
{
	int y_pixels_size = buf->stride * buf->height;
	memset(buf->mmap, y, y_pixels_size);
	memset16(buf->mmap + y_pixels_size, cbcr, y_pixels_size/4);
}

static void
fill_rgba_content(struct buffer *my_buf, uint8_t a, uint8_t r, uint8_t g, uint8_t b)
{
	int x = 0, y = 0;
	uint32_t *pix;

	assert(my_buf->mmap);

	for (y = 0; y < my_buf->height; y++) {
		pix = (uint32_t *)(my_buf->mmap + y * my_buf->stride);
		for (x = 0; x < my_buf->width; x++) {
			*pix++ = (a << 24) | ((r) << 16) | ((g) << 8) | b;
		}
	}
}

static void
unmap_bo(struct buffer *my_buf)
{
	int ret;
	ret = gbm_perform(GBM_PERFORM_CPU_UNMAP_FOR_BO, my_buf->bo);
	if (ret != GBM_ERROR_NONE)
		printf("bo unmap failed: ret = %d\n", ret);
}

static int
fb_connect(struct buffer *my_buf)
{
	char fb_dev[64] = DRV_NODE;
	my_buf->fb_fd = open(fb_dev, O_RDWR | O_CLOEXEC, S_IRWXU);
	if (my_buf->fb_fd < 0) {
		printf("opening %s failed\n", fb_dev);
		return 0;
	}

	my_buf->gbm = gbm_create_device(my_buf->fb_fd);
	if (!my_buf->gbm) {
		printf("opening gbm device with fd = %d failed\n", my_buf->fb_fd);
		return 0;
	}

	return 1;
}

static int
drm_connect(struct buffer *my_buf)
{
	/* This won't work with card0 as we need to be authenticated; instead,
	 * boot with drm.rnodes=1 and use that. */
	my_buf->drm_fd = open(DRV_NODE, O_RDWR | O_CLOEXEC, S_IRWXU);
	if (my_buf->drm_fd < 0)
		return 0;

	my_buf->gbm = gbm_create_device(my_buf->drm_fd);
	if (!my_buf->gbm)
		return 0;

	return 1;
}

static void
fb_shutdown(struct buffer *my_buf)
{
	gbm_device_destroy(my_buf->gbm);
	close(my_buf->fb_fd);
}

static void
drm_shutdown(struct buffer *my_buf)
{
	printf("%s\b", __func__);
	gbm_device_destroy(my_buf->gbm);
	close(my_buf->drm_fd);
}

static int
driver_connect(struct buffer *my_buf)
{
#ifdef DRV_FB
		return fb_connect(my_buf);
#elif defined(DRV_DRM)
		return drm_connect(my_buf);
#endif
		return -EINVAL;
}

static void
driver_shutdown(struct buffer *buf)
{
#ifdef DRV_FB
		fb_shutdown(buf);
#elif defined(DRV_DRM)
		drm_shutdown(buf);
#endif
}

static int
alloc_bo(struct buffer *my_buf)
{
	/* XXX: try different tiling modes for testing FB modifiers. */
	int ret;

	assert(my_buf->gbm);

	my_buf->bo = gbm_bo_create(my_buf->gbm, my_buf->width, my_buf->height, my_buf->format, my_buf->flags);

	if (!my_buf->bo)
		return 0;

	my_buf->stride = gbm_bo_get_stride(my_buf->bo);
	my_buf->bpp = gbm_bo_get_bpp(my_buf->bo);

	return 1;
}

static void
free_bo(struct buffer *my_buf)
{
	gbm_bo_destroy(my_buf->bo);
}

static void
buffer_release(void *data, struct wl_buffer *buffer)
{
	struct buffer *my_buf = data;
	my_buf->busy = 0;
}

static const struct wl_buffer_listener buffer_listener = {
	buffer_release
};

static void
gbmbuf_create_succeeded(void *data,
		 struct gbm_buffer_params *params,
		 struct wl_buffer *new_buffer)
{
	struct buffer *my_buf = data;

	my_buf->buffer = new_buffer;

	wl_buffer_add_listener(my_buf->buffer, &buffer_listener, my_buf);

	gbm_buffer_params_destroy(params);
}

static void
gbmbuf_create_failed(void *data, struct gbm_buffer_params *params)
{
	struct buffer *my_buf = data;

	my_buf->buffer = NULL;

	gbm_buffer_params_destroy(params);

	printf("gbm_buffer_params.create failed\n");
}

static const struct gbm_buffer_params_listener gbmbuf_params_listener = {
	gbmbuf_create_succeeded,
	gbmbuf_create_failed
};

static int
create_gbmbuf_buffer(struct display *display, struct buffer *buffer,
		     int width, int height, uint32_t format, uint32_t flags)
{
	struct gbm_buffer_params *params;
	uint64_t modifier;
	struct window *window;

	window = display->window;

	if (driver_connect(buffer) == 0) {
		fprintf(stderr, "driver %s connect failed \n",DRV_NODE);
		goto error;
	}

	buffer->width = width;
	buffer->height = height;
	buffer->format = format;
	buffer->flags = flags;

	if (!alloc_bo(buffer)) {
		fprintf(stderr, "alloc_bo failed\n");
		goto error1;
	}

	buffer->bpp = gbm_bo_get_bpp(buffer->bo);
	buffer->gbmbuf_fd = gbm_bo_get_fd(buffer->bo);
	if (buffer->gbmbuf_fd < 0) {
		fprintf(stderr, "error: gbmbuf_fd < 0\n");
		goto error2;
	}

	if (GBM_ERROR_NONE != gbm_perform(GBM_PERFORM_GET_METADATA_ION_FD, buffer->bo, &buffer->gbmbuf_metafd)) {
		fprintf(stderr, "error: gbmbuf_metafd < 0\n");
		goto error2;
	}

	if (!map_bo(buffer)) {
		fprintf(stderr, "map_bo failed\n");
		goto error2;
	}

	if (format == GBM_FORMAT_NV12)
		fill_nv12_content(buffer, COLOR_Y, COLOR_CBCR);
	else if (format == GBM_FORMAT_ABGR8888)
		fill_rgba_content(buffer, 255, 255, 0, 0); //blue color

	unmap_bo(buffer);

	/* We now have a gbmbuf! It should contain no tiles i.e. linear of misc colours,
	  and be mappable, either as ARGB8888, or XRGB8888. */
	modifier = 0;

	params = gbm_buffer_backend_create_params(display->gbmbuf);

	if (!params) {
		goto error2;
	}

	gbm_buffer_params_create(params,
				    buffer->gbmbuf_fd,
				    buffer->gbmbuf_metafd,
				    buffer->width,
				    buffer->height,
				    buffer->format,
				    buffer->flags);

	gbm_buffer_params_add_listener(params, &gbmbuf_params_listener, buffer);

	wl_display_roundtrip(display->display);

	if (!buffer->buffer)
		goto error2;

	/* params is destroyed by the event handlers */

	return 0;

error2:
	free_bo(buffer);
error1:
	driver_shutdown(buffer);
error:
	return -1;
}

static void
xdg_surface_handle_configure(void *data, struct xdg_surface *surface,
			     uint32_t serial)
{
	struct window *window = data;

	xdg_surface_ack_configure(surface, serial);

	if (window->wait_for_configure) {
		redraw(window, NULL, 0);
		window->wait_for_configure = false;
	}
}

static const struct xdg_surface_listener xdg_surface_listener = {
	xdg_surface_handle_configure,
};

static void
xdg_toplevel_handle_configure(void *data, struct xdg_toplevel *toplevel,
			      int32_t width, int32_t height,
			      struct wl_array *states)
{
	printf("handle configure\n");
}

static void
xdg_toplevel_handle_close(void *data, struct xdg_toplevel *xdg_toplevel)
{
	running = 0;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	xdg_toplevel_handle_configure,
	xdg_toplevel_handle_close,
};

static struct window *
create_window(struct display *display, int width, int height)
{
	struct window *window;

	window = calloc(1, sizeof *window);
	if (!window)
		return NULL;

	window->callback = NULL;
	window->display = display;
	window->width = width;
	window->height = height;
	window->surface = wl_compositor_create_surface(display->compositor);

	if (display->wm_base) {
		window->xdg_surface =
			 xdg_wm_base_get_xdg_surface(display->wm_base,
						    window->surface);

		assert(window->xdg_surface);

		xdg_surface_add_listener(window->xdg_surface,
					 &xdg_surface_listener, window);

		window->xdg_toplevel =
			xdg_surface_get_toplevel(window->xdg_surface);

		assert(window->xdg_toplevel);

		xdg_toplevel_add_listener(window->xdg_toplevel,
					  &xdg_toplevel_listener, window);

		xdg_toplevel_set_title(window->xdg_toplevel, "simple-gbmbuf");

		window->wait_for_configure = true;
		wl_surface_commit(window->surface);
	} else if (display->fshell) {
		zwp_fullscreen_shell_v1_present_surface(display->fshell,
							window->surface,
							ZWP_FULLSCREEN_SHELL_V1_PRESENT_METHOD_DEFAULT,
							NULL);
	} else {
		assert(0);
	}

	/* Initialise damage to full surface, so the padding gets painted */
	wl_surface_damage(window->surface, 0, 0, INT32_MAX, INT32_MAX);

	return window;
}

static void
destroy_window(struct window *window)
{
	int i;

	if (window->callback)
		wl_callback_destroy(window->callback);

	for (i = 0; i < NUM_GBM_BUFFERS; i++) {
		if (!window->buffers[i].buffer)
			continue;

		wl_buffer_destroy(window->buffers[i].buffer);
		free_bo(&window->buffers[i]);
		close(window->buffers[i].gbmbuf_fd);
		driver_shutdown(&window->buffers[i]);
	}

	if (window->xdg_toplevel)
		xdg_toplevel_destroy(window->xdg_toplevel);
	if (window->xdg_surface)
		xdg_surface_destroy(window->xdg_surface);
	wl_surface_destroy(window->surface);
	free(window);
}

static struct buffer *
window_next_buffer(struct window *window, uint32_t format, uint32_t flags)
{
	struct buffer *buffer = NULL;
	struct wl_display *display = window->display;
	int ret = 0;
	int i = 0;

	for (i=0; i < NUM_GBM_BUFFERS; i++) {
		if (window->buffers[i].busy == 0) {
			buffer = &window->buffers[i];
			break;
		}
	}

	if (buffer == NULL) {
		return NULL;
	}

	if (!buffer->buffer) {
		ret = create_gbmbuf_buffer(window->display, buffer,
					   window->width, window->height, format, flags);

		if (ret < 0)
			return NULL;
	}

	return buffer;
}

static const struct wl_callback_listener frame_listener;

static void
redraw(void *data, struct wl_callback *callback, uint32_t time)
{
	struct window *window = data;
	struct buffer *buffer;

	buffer = window_next_buffer(window, GBM_FORMAT_ABGR8888, GBM_BO_USE_RENDERING);
	if (!buffer) {
			fprintf(stderr,
				!callback ? "Failed to create the first buffer.\n" :
				"All buffers busy at redraw(). Server bug?\n");
			abort();
	}

	wl_surface_attach(window->surface, buffer->buffer, 0, 0);
	wl_surface_damage(window->surface, 0, 0, window->width, window->height);

	if (callback)
		wl_callback_destroy(callback);

	window->callback = wl_surface_frame(window->surface);
	wl_callback_add_listener(window->callback, &frame_listener, window);
	wl_surface_commit(window->surface);

	buffer->busy = 1;
}

static const struct wl_callback_listener frame_listener = {
	redraw
};

static void
xdg_wm_base_ping(void *data, struct xdg_wm_base *shell, uint32_t serial)
{
	xdg_wm_base_pong(shell, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
	xdg_wm_base_ping,
};

static void
registry_handle_global(void *data, struct wl_registry *registry,
		       uint32_t id, const char *interface, uint32_t version)
{
	struct display *d = data;

	if (strcmp(interface, "wl_compositor") == 0) {
		d->compositor =
			wl_registry_bind(registry,
					 id, &wl_compositor_interface, version);
	} else if (strcmp(interface, "xdg_wm_base") == 0) {
		d->wm_base = wl_registry_bind(registry,
					      id, &xdg_wm_base_interface, 1);
		if (!d->wm_base) {
			fprintf(stderr, "d->wm_base is NULL\n");
			return;
		}
		xdg_wm_base_add_listener(d->wm_base, &wm_base_listener, d);
	} else if (strcmp(interface, "zwp_fullscreen_shell_v1") == 0) {
		d->fshell = wl_registry_bind(registry,
					     id, &zwp_fullscreen_shell_v1_interface, 1);
	} else if (strcmp(interface, "gbm_buffer_backend") == 0) {
		d->gbmbuf = wl_registry_bind(registry,
					      id, &gbm_buffer_backend_interface, 1);
	}
}

static void
registry_handle_global_remove(void *data, struct wl_registry *registry,
			      uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	registry_handle_global,
	registry_handle_global_remove
};

static struct display *
create_display(void)
{
	struct display *display;
	int done;

	display = malloc(sizeof *display);
	if (display == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	display->display = wl_display_connect(NULL);
	assert(display->display);

	/* XXX: fake, because the compositor does not yet advertise anything */
	display->xrgb8888_format_found = 1;

	display->registry = wl_display_get_registry(display->display);

	wl_registry_add_listener(display->registry,
				 &registry_listener, display);

	done = 0;
	while (!done) {
		if (wl_display_roundtrip(display->display) != -1)
			done = 1;
	}

	if (!display->wm_base)
		printf("can't get xdg_shell\n");

	if (display->gbmbuf == NULL) {
		fprintf(stderr, "No gbmbuf global\n");
		exit(1);
	}

	wl_display_roundtrip(display->display);

	return display;
}

static void
destroy_display(struct display *display)
{
	if (display->gbmbuf)
		gbm_buffer_backend_destroy(display->gbmbuf);

	if (display->wm_base)
		xdg_wm_base_destroy(display->wm_base);

	if (display->fshell)
		zwp_fullscreen_shell_v1_release(display->fshell);

	if (display->compositor)
		wl_compositor_destroy(display->compositor);

	wl_registry_destroy(display->registry);
	wl_display_flush(display->display);
	wl_display_disconnect(display->display);
	free(display);
}

static void
signal_int(int signum)
{
	running = 0;
}

int
main(int argc, char **argv)
{
	struct sigaction sigint;
	struct display *display;
	struct window *window;
	int width = 256, height = 256;

	int ret = 0;
	display = create_display();
	if (!display) {
		fprintf(stderr, "can't connect weston display\n");
		return 1;
	}

	window = create_window(display, width, height);
	if (!window)
		return 1;

	display->window = window;

	sigint.sa_handler = signal_int;
	sigemptyset(&sigint.sa_mask);
	sigint.sa_flags = SA_RESETHAND;
	sigaction(SIGINT, &sigint, NULL);

	/* Initialise damage to full surface, so the padding gets painted */
	wl_surface_damage(window->surface, 0, 0, window->width, window->height);

	if (!window->wait_for_configure)
		redraw(window, NULL, 0);

	ret = 0;
	while (running && ret != -1) {
		ret = wl_display_dispatch(display->display);
	}

	fprintf(stderr, "simple-gbmbuf exiting\n");
	destroy_window(window);
	destroy_display(display);

	return 0;
}
