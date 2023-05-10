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

#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <libweston/libweston.h>
#include "gbm_priv.h"
#include "gbm-buffer-backend.h"
#include "linux-dmabuf.h"
#include "screen-capture.h"
#include "gbm-buffer-backend-server-protocol.h"
#include "screen-capture-server-protocol.h"
#include "../sdm-service/compositor-sdm-output.h"

#define TIMEOUT_MS 64

static struct drm_output *
screen_capture_create_virtual_display(struct weston_output *mirror_output)
{
	struct drm_output *output = NULL;

	output = zalloc(sizeof *output);
	if (output == NULL)
		goto err;

	//TODO: Create SDM virtual display

	/* If mirror output can be pluggable in future, update output->base in drm_assign_planes */
	output->base = *mirror_output;
	return output;

err:
	return NULL;
}

static int
screen_capture_destroy_virtual_display(struct drm_output *output)
{
	if (output == NULL)
		goto err;

	//TODO: Destroy SDM virtual display

	return 0;

err:
	return -1;
}

static void
screen_capture_create_screen(struct wl_client *client,
		struct wl_resource *resource,
		struct wl_resource *output_resource,
		uint32_t width,
		uint32_t height)
{
	struct screen_capture *screen_cap = NULL;
	struct weston_head *mirror_head;
	struct weston_output *mirror_output;
	struct drm_backend *b;

	SC_PROTOCOL_LOG(SC_LOG_DBG,"screen_capture_create_screen::Invoked\n");

	screen_cap = wl_resource_get_user_data(resource);
	if (!screen_cap) {
		wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT, "screen capture is null!");
		screen_capture_send_failed(resource);
		return;
	}

	if (screen_cap->virtual_output) {
		wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT, "screen is already created!");
		screen_capture_send_failed(resource);
		return;
	}

	mirror_head = wl_resource_get_user_data(output_resource);
	mirror_output = mirror_head->output;
	screen_cap->virtual_output = screen_capture_create_virtual_display(mirror_output);
	if (!screen_cap->virtual_output) {
		SC_PROTOCOL_LOG(SC_LOG_ERR,"Error! can't create SDM virtual display\n");
		screen_capture_send_failed(resource);
		return;
	}

	screen_cap->mirror_output_id = mirror_output->id;
	screen_cap->width = width;
	screen_cap->height = height;
	screen_cap->main_output = (struct  drm_output*)mirror_output;
	if (width != mirror_output->width || height != mirror_output->height) {
		SC_PROTOCOL_LOG(SC_LOG_WARN, "not capture the fullscreen, CWB doesn't\
			support, fallback to GPU\n");
		screen_cap->force_gpu = true;
	}

	b = (struct drm_backend *)screen_cap->compositor->backend;
	b->screen_cap = screen_cap;

	screen_capture_send_created(resource);
	SC_PROTOCOL_LOG(SC_LOG_DBG,"screen_capture_create_screen::Exited\n");
}

static void
wait_for_release(struct screen_capture_buffer *cap_buf)
{
	int error = 0;
	int fence = cap_buf->release_fence_fd;

	if (fence != -1) {
		struct pollfd poll_fd = {0};
		poll_fd.fd = fence;
		poll_fd.events = POLLIN;
		error = poll(&poll_fd, 1, 1000);
		if (error <= 0) {
			SC_PROTOCOL_LOG(SC_LOG_ERR,"fail to wait for display WB2 composition!\n");
			/* What happens if poll fails? */
		} else {
			close(fence);
		}
	}
}

static void screen_capture_timeout(void *data)
{
	struct screen_capture *screen_cap = data;
	struct screen_capture_buffer *cap_buf = NULL;

	if (data == NULL)
		return;

	SC_PROTOCOL_LOG(SC_LOG_ERR,"capture timeout happen\n");

	/*free resource of buffer current*/
	cap_buf = screen_cap->current;
	if (cap_buf && cap_buf->cap_fence_source) {
		weston_buffer_reference(&cap_buf->buf_ref, NULL);
		wl_event_source_remove(cap_buf->cap_fence_source);
		close(cap_buf->release_fence_fd);
		gbm_bo_destroy(cap_buf->bo);
		free(cap_buf);
		screen_cap->current = NULL;
	}

	/*free resource of buffer next*/
	cap_buf = screen_cap->next;
	if (cap_buf && cap_buf->cap_fence_source) {
		weston_buffer_reference(&cap_buf->buf_ref, NULL);
		wl_event_source_remove(cap_buf->cap_fence_source);
		close(cap_buf->release_fence_fd);
		gbm_bo_destroy(cap_buf->bo);
		free(cap_buf);
		screen_cap->next = NULL;
	}

	screen_capture_post_exit(screen_cap);
}

static bool
screen_capture_exit(struct screen_capture *screen_cap)
{
	struct drm_backend *b = (struct drm_backend *)screen_cap->compositor->backend;
	struct screen_capture_buffer *cap_buf, *next;

	if (screen_cap == NULL)
		return true;

	/*flush one frame to let CWB unregister display*/
	if (screen_cap->main_output)
		weston_output_damage(&screen_cap->main_output->base);

	/* Clear those buffers which have not been consumed yet. */
	wl_list_for_each_safe(cap_buf, next, &screen_cap->attached_buf_list, link) {
		wl_list_remove(&cap_buf->link);
		weston_buffer_reference(&cap_buf->buf_ref, NULL);
		gbm_bo_destroy(cap_buf->bo);
		free(cap_buf);
	}

	/* Clear those buffers which have not been consumed yet. */
	wl_list_for_each_safe(cap_buf, next, &screen_cap->free_buf_list, link) {
		wl_list_remove(&cap_buf->link);
		gbm_bo_destroy(cap_buf->bo);
		free(cap_buf);
	}

	if (screen_cap->current || screen_cap->next) {
		if (screen_cap->timeout_source)
			wl_event_source_timer_update(screen_cap->timeout_source, TIMEOUT_MS);
		return false;
	}

	return true;
}

static void
screen_capture_destroy_screen(struct wl_client *client,
		struct wl_resource *resource)
{
	struct screen_capture *screen_cap = NULL;

	SC_PROTOCOL_LOG(SC_LOG_DBG,"screen_capture_destroy_screen::Invoked\n");

	screen_cap = wl_resource_get_user_data(resource);
	if (!screen_cap) {
		wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT, "screen capture is null!");
		screen_capture_send_destroyed(resource);
		return;
	}

	if (!screen_cap->virtual_output) {
		wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT, "screen is already destroyed!");
		screen_capture_send_destroyed(resource);
		return;
	}

	screen_cap->output_destroy_pending = true;
	if (screen_capture_exit(screen_cap))
		screen_capture_post_exit(screen_cap);
}

static void
screen_capture_start(struct wl_client *client,
		struct wl_resource *resource)
{
	struct screen_capture *screen_cap = NULL;

	SC_PROTOCOL_LOG(SC_LOG_DBG,"screen_capture_start::Invoked\n");
	screen_cap = wl_resource_get_user_data(resource);
	if (!screen_cap) {
		wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT, "screen capture is null!");
		return;
	}

	screen_cap->enabled = true;
	screen_capture_send_started(resource);

	SC_PROTOCOL_LOG(SC_LOG_DBG,"screen_capture_start::Exited\n");
}

static bool is_capture_format_supported(uint32_t format)
{
	switch(format) {
		/*only verify limited formats */
		case GBM_FORMAT_ABGR8888:
		case GBM_FORMAT_ARGB8888:
		case GBM_FORMAT_RGB565:
		case GBM_FORMAT_RGB888:
			return true;
		default:
			return false;
	}
}

static bool create_capture_buffer(struct screen_capture *screen_cap,
	struct gbm_bo *bo, struct weston_buffer *buffer, int buf_fd, int type)
{
	int format;
	int ubwc_status = 0;
	int secure_status = 0;
	struct screen_capture_buffer *capture_buf = NULL;

	if (!screen_cap || !bo)
		return false;

	format = gbm_bo_get_format(bo);

	if (!is_capture_format_supported(format)) {
		SC_PROTOCOL_LOG(SC_LOG_DBG,"not support capture buffer format!\n");
		return false;
	}

	gbm_perform(GBM_PERFORM_GET_SECURE_BUFFER_STATUS, bo, &secure_status);
	/*don't verify secure buffer in current stage*/
	if (secure_status) {
		SC_PROTOCOL_LOG(SC_LOG_DBG,"not support secure capture buffer!\n");
		return false;
	}
	gbm_perform(GBM_PERFORM_GET_UBWC_STATUS, bo, &ubwc_status);

	if (buffer->width != screen_cap->width ||
				buffer->height != screen_cap->height) {
		SC_PROTOCOL_LOG(SC_LOG_ERR,"invalid w/h of capture buffer! buffer: w=%d,\
			h=%d; screen: w=%d, h=%d\n",buffer->width, buffer->height,
			screen_cap->width, screen_cap->height);
		return false;
	}

	capture_buf = zalloc(sizeof *capture_buf);
	if (capture_buf == NULL) {
		SC_PROTOCOL_LOG(SC_LOG_ERR,"no memory to create capture buffer!\n");
		return false;
	}

	capture_buf->buffer = buffer;
	capture_buf->release_fence_fd = -1;
	capture_buf->type = type;
	capture_buf->buffer_fd = buf_fd;
	capture_buf->format = format;
	capture_buf->bo = bo;
	capture_buf->is_secure = secure_status > 0 ? true : false;
	capture_buf->type = type;
	capture_buf->is_ubwc = ubwc_status > 0 ? true : false;

	wl_list_init(&capture_buf->link);
	wl_list_insert(screen_cap->attached_buf_list.prev, &capture_buf->link);

	/* Increase the buf refcnt here. */
	weston_buffer_reference(&capture_buf->buf_ref, buffer);
	SC_PROTOCOL_LOG(SC_LOG_DBG,"screen capture buffer is attached to list!\n");

	return true;
}

static void
screen_capture_frame(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *buffer_resource, uint32_t type)
{
	struct screen_capture *screen_cap = NULL;
	struct screen_capture_buffer *cap_buf;
	struct weston_buffer *buffer =
		weston_buffer_from_resource(buffer_resource);
	struct drm_backend *b;
	struct gbm_buffer *gbm_buf;
	struct linux_dmabuf_buffer *linux_dmabuf;
	int buffer_fd = -1;
	int format = 0;

	if (buffer == NULL) {
		wl_resource_post_no_memory(resource);
		return;
	}

	if (type < SCREEN_CAPTURE_TYPE_MIXER_OUT || type > SCREEN_CAPTURE_TYPE_GPU_OUT) {
		SC_PROTOCOL_LOG(SC_LOG_ERR,"invalid capture buffer type\n");
		return false;
	}

	screen_cap = wl_resource_get_user_data(resource);

	/* Skip attach buffer if screen capture doesn't create screen or the screen
	 * is pending to destroy
	 */
	if (!screen_cap || !screen_cap->virtual_output || screen_cap->output_destroy_pending) {
		SC_PROTOCOL_LOG(SC_LOG_ERR,"screen capture is not ready!\n");
		return;
	}

	wl_list_for_each(cap_buf, &screen_cap->attached_buf_list, link) {
		/* skip buffer which have already existed in the buffer list */
		if (cap_buf->buffer == buffer) {
			SC_PROTOCOL_LOG(SC_LOG_DBG,"attach the existed capture buffer!\n");
			return;
		}
	}

	wl_list_for_each(cap_buf, &screen_cap->free_buf_list, link) {
		/* reuse the buffer in the free list */
		if (cap_buf->buffer == buffer) {
			wl_list_remove(&cap_buf->link);
			wl_list_init(&cap_buf->link);
			wl_list_insert(screen_cap->attached_buf_list.prev, &cap_buf->link);
			weston_buffer_reference(&cap_buf->buf_ref, buffer);
			cap_buf->type = type;
			weston_output_damage(&screen_cap->main_output->base);
			return;
		}
	}

	b = (struct drm_backend *)screen_cap->compositor->backend;

	gbm_buf = gbm_buffer_get(buffer->resource);
	linux_dmabuf = linux_dmabuf_buffer_get(buffer->resource);
	/* Only support GBM buffer and linux dma buffer now. */
	if (!gbm_buf && !linux_dmabuf) {
		return;
	}

	struct gbm_bo *bo;
	if (gbm_buf) {
		buffer->width = gbm_buf->width;
		buffer->height = gbm_buf->height;
		buffer_fd = gbm_buf->fd;
		format = gbm_buf->format;
		struct gbm_buf_info gbm_bufinfo = {
			.fd           = buffer_fd,
			.metadata_fd  = gbm_buf->metadata_fd,
			.width        = gbm_buf->width,
			.height       = gbm_buf->height,
			.format       = gbm_buf->format
		};
		bo = gbm_bo_import(b->gbm, GBM_BO_IMPORT_GBM_BUF_TYPE, &gbm_bufinfo,
				GBM_BO_USE_SCANOUT);
	} else {
		buffer->width = linux_dmabuf->attributes.width;
		buffer->height = linux_dmabuf->attributes.height;
		buffer_fd = linux_dmabuf->attributes.fd[0];
		format = linux_dmabuf->attributes.format;
		struct gbm_import_fd_modifier_data gbm_dmabuf = {
			.width   = buffer->width,
			.height  = buffer->height,
			.format  = format,
			.num_fds = 1,
			.modifier = linux_dmabuf->attributes.modifier[0]
		};

		gbm_dmabuf.fds[0] = buffer_fd;
		bo = gbm_bo_import(b->gbm, GBM_BO_IMPORT_FD_MODIFIER, &gbm_dmabuf,
				GBM_BO_USE_SCANOUT);
	}

	if (!create_capture_buffer(screen_cap, bo, buffer, buffer_fd, type)) {
		gbm_bo_destroy(bo);
		return;
	}

	weston_output_damage(&screen_cap->main_output->base);
}

WL_EXPORT bool
is_capture_ready(struct screen_capture *screen_cap, struct weston_output *output)
{
	if (!screen_cap)
		return false;

	return (screen_cap->enabled && screen_cap->mirror_output_id == output->id &&
			!screen_cap->output_destroy_pending && !screen_cap->destroy_pending);
}

WL_EXPORT void
screen_capture_attach(struct weston_compositor *compositor,
		struct weston_buffer *buffer)
{
	struct gbm_buffer *gbm_buf = NULL;
	struct drm_backend *b = (struct drm_backend *)compositor->backend;
	struct screen_capture *screen_cap = b->screen_cap;
	struct screen_capture_buffer *cap_buf;

	/* Skip attach buffer if screen capture doesn't create screen or the screen
	 * is pending to destroy
	 */
	if (!screen_cap || !screen_cap->virtual_output ||
		screen_cap->output_destroy_pending)
		return;

	/* Only support GBM buffer now. */
	if (!buffer ||
		wl_shm_buffer_get(buffer->resource) ||
		linux_dmabuf_buffer_get(buffer->resource)) {
		return;
	}

	/* Screen capture buffer can't be NULL */
	gbm_buf = gbm_buffer_get(buffer->resource);
	if (!gbm_buf) {
		return;
	}

	wl_list_for_each(cap_buf, &screen_cap->attached_buf_list, link) {
		/* skip buffer which have already existed in the buffer list, this situation happends on
		 * gl_renderer_create_surface which call gl_renderer_attach first, weston_surface_attach
		 * call gl_renderer_attach second time for the same buffer.
		 */
		if (cap_buf->buffer == buffer) {
			SC_PROTOCOL_LOG(SC_LOG_DBG,"attach the existed capture buffer!\n");
			return;
		}
	}

	wl_list_for_each(cap_buf, &screen_cap->free_buf_list, link) {
		if (cap_buf->buffer == buffer) {
			wl_list_remove(&cap_buf->link);
			wl_list_init(&cap_buf->link);
			wl_list_insert(screen_cap->attached_buf_list.prev, &cap_buf->link);
			weston_buffer_reference(&cap_buf->buf_ref, buffer);
			return;
		}
	}

	if (gbm_buf->flags & GBM_BUFFER_PARAMS_FLAGS_SCREEN_CAPTURE) {
		struct gbm_bo *bo;
		struct gbm_buf_info gbm_bufinfo = {
			.fd           = gbm_buf->fd,
			.metadata_fd  = gbm_buf->metadata_fd,
			.width        = gbm_buf->width,
			.height       = gbm_buf->height,
			.format       = gbm_buf->format
		};
		bo = gbm_bo_import(b->gbm, GBM_BO_IMPORT_GBM_BUF_TYPE, &gbm_bufinfo,
 				GBM_BO_USE_SCANOUT);
		create_capture_buffer(screen_cap, bo, buffer, gbm_buf->fd, SCREEN_CAPTURE_TYPE_MIXER_OUT);
	}
}

WL_EXPORT void screen_capture_post_exit(struct screen_capture *screen_cap)
{
	struct drm_backend *b = (struct drm_backend *)screen_cap->compositor->backend;

	if (screen_cap->virtual_output) {
			if (screen_capture_destroy_virtual_display(screen_cap->virtual_output)) {
				SC_PROTOCOL_LOG(SC_LOG_ERR,"fail to destroy virtual display\n");
			}
			free(screen_cap->virtual_output);
			screen_cap->virtual_output = NULL;
	}

	if (screen_cap->output_destroy_pending) {
		if (screen_cap->resource && !screen_cap->destroy_pending)
			screen_capture_send_destroyed(screen_cap->resource);
		SC_PROTOCOL_LOG(SC_LOG_DBG,"screen_capture_destroy_screen::Exited\n");
		screen_cap->output_destroy_pending = false;
	}

	if (screen_cap->destroy_pending) {
		if (screen_cap->timeout_source)
			wl_event_source_remove(screen_cap->timeout_source);
		b->screen_cap = NULL;
		free(screen_cap);
	}
}

WL_EXPORT bool
is_screen_capture_buffer(struct weston_buffer *buffer)
{
	if (buffer) {
		struct gbm_buffer *gbm_buf =
				gbm_buffer_get(buffer->resource);

		if (gbm_buf &&
				gbm_buf->flags & GBM_BUFFER_PARAMS_FLAGS_SCREEN_CAPTURE) {
			return true;
		}
	}

	return false;
}

WL_EXPORT bool
is_screen_capture_view(struct weston_view *ev)
{
	if (ev->is_capture_view)
		return true;

	if (ev && ev->surface && ev->surface->buffer_ref.buffer) {
		struct gbm_buffer *gbm_buf =
				gbm_buffer_get(ev->surface->buffer_ref.buffer->resource);

		if (gbm_buf &&
				gbm_buf->flags & GBM_BUFFER_PARAMS_FLAGS_SCREEN_CAPTURE) {
			/*
			 * ev->surface->buffer_ref.buffer will be NULL during close
			 * animation of screen capture application, if no hint is
			 * stored, the last frame of screen capture application will be
			 * used as texture during composition, which is not expected.
			 */
			ev->is_capture_view = true;
			return true;
		}
	}

	return false;
}

static void
screen_capture_stop(struct wl_client *client,
		struct wl_resource *resource)
{
	struct screen_capture *screen_cap = NULL;

	SC_PROTOCOL_LOG(SC_LOG_DBG,"screen_capture_stop::Invoked\n");

	screen_cap = wl_resource_get_user_data(resource);
	if (!screen_cap) {
		wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT, "screen capture is null!");
		return;
	}

	screen_cap->enabled = false;
	screen_capture_send_stopped(resource);

	SC_PROTOCOL_LOG(SC_LOG_DBG,"screen_capture_stop::Exited\n");
}

static void
screen_capture_destroy(struct wl_client *client,
		struct wl_resource *resource)
{
	SC_PROTOCOL_LOG(SC_LOG_DBG,"screen_capture_destroy::Invoked\n");

	wl_resource_destroy(resource);

	SC_PROTOCOL_LOG(SC_LOG_DBG,"screen_capture_destroy::Exited\n");
}

static void
destroy_screen_capture(struct wl_resource *resource)
{
	struct screen_capture *screen_cap = NULL;

	screen_cap = wl_resource_get_user_data(resource);
	if (!screen_cap) {
		wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT, "screen capture is null!");
		return;
	}
	screen_cap->destroy_pending = true;
	if (screen_cap) {
		/* call exit again to avoid expected client crash */
		if (screen_capture_exit(screen_cap)) {
			screen_capture_post_exit(screen_cap);
		}
	}
	wl_resource_set_user_data(resource, NULL);
}

static const struct screen_capture_interface screen_capture_implementation = {
	screen_capture_destroy,
	screen_capture_create_screen,
	screen_capture_destroy_screen,
	screen_capture_start,
	screen_capture_stop,
	screen_capture_frame
};

static void screen_capture_init(struct screen_capture *sc, struct weston_compositor *compositor)
{
	struct wl_event_loop *loop = wl_display_get_event_loop(compositor->wl_display);

	sc->compositor = compositor;
	sc->enabled = false;
	sc->fallback_gpu = false;
	sc->force_gpu = false;
	sc->mirror_output_id = 0; /* mirror the primary display by default */
	sc->virtual_output = NULL;
	sc->view = NULL;
	sc->next = sc->current = NULL;
	sc->destroy_pending = false;
	sc->output_destroy_pending = false;
	sc->timeout_source =  wl_event_loop_add_timer(loop, screen_capture_timeout,
                    sc);

	wl_list_init(&sc->attached_buf_list);
	wl_list_init(&sc->free_buf_list);
}

static void
bind_screen_capture(struct wl_client *client,
		void *data, uint32_t version, uint32_t id)
{
	struct weston_compositor *compositor = data;
	struct wl_resource *resource;
	struct screen_capture *screen_cap = NULL;
	struct drm_backend *b = (struct drm_backend *)compositor->backend;

	SC_PROTOCOL_LOG(SC_LOG_DBG,"bind_screen_capture::Invoked\n");

	/* User needs to guarantee only one instance is running */
	if (b->screen_cap) {
		SC_PROTOCOL_LOG(SC_LOG_ERR,"another client has already started screen capture!\n");
		wl_client_post_no_memory(client);
		return;
	}

	screen_cap = zalloc(sizeof *screen_cap);
	if (screen_cap == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	resource = wl_resource_create(client, &screen_capture_interface,
					version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		free(screen_cap);
		return;
	}

	screen_capture_init(screen_cap, compositor);

	wl_resource_set_implementation(resource,
			&screen_capture_implementation,
			screen_cap, destroy_screen_capture);

	screen_cap->resource = resource;

	SC_PROTOCOL_LOG(SC_LOG_DBG,"bind_screen_capture::Exited\n");
}

/** Advertise screen_capture support
 *
 * Calling this initializes the screen_capture protocol support, so that
 * the interface will be advertised to clients. Essentially it creates a
 * global. Do not call this function multiple times in the compositor's
 * lifetime. There is no way to deinit explicitly, globals will be reaped
 * when the wl_display gets destroyed.
 *
 * \param compositor The compositor to init for.
 * \return Zero on success, -1 on failure.
 */
WL_EXPORT int
screen_capture_setup(struct weston_compositor *compositor)
{
	SC_PROTOCOL_LOG(SC_LOG_DBG,"screen_capture_setup::Invoked\n");

	if (!wl_global_create(compositor->wl_display,
				&screen_capture_interface, 1,
				compositor, bind_screen_capture)) {
		return -1;
	}
	SC_PROTOCOL_LOG(SC_LOG_DBG,"screen_capture_setup::Exited\n");

	return 0;
}
