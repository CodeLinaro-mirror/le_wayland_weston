/*
 * Copyright (c) 2017, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 *   - Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   - Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   - Neither the name of The Linux Foundation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 *IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include <unistd.h>
#include <sys/types.h>

#include "compositor.h"
#include "gbm-buffer-backend.h"
#include "gbm-buffer-backend-server-protocol.h"

static void
gbm_wl_buffer_destroy(struct wl_client *client,
	struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct wl_buffer_interface gbm_buffer_implementation = {
	gbm_wl_buffer_destroy
};

WL_EXPORT struct gbm_buffer *
gbm_buffer_get(struct wl_resource *resource)
{
	struct gbm_buffer *buffer;

	if (!resource)
		return NULL;

	if (!wl_resource_instance_of(resource, &wl_buffer_interface,
				     &gbm_buffer_implementation))
		return NULL;

	buffer = wl_resource_get_user_data(resource);
	assert(buffer);
	assert(buffer->buffer_resource == resource);

	return buffer;
}

static void
destroy_gbm_buffer(struct wl_resource *resource)
{
	struct gbm_buffer *buffer = wl_resource_get_user_data(resource);

	free(buffer);
}

static void
gbm_buffer_backend_create_buffer(struct wl_client *client,
		struct wl_resource *backend_resource,
		uint32_t id,
		int32_t fd,
		int32_t metadata_fd,
		uint32_t width,
		uint32_t height,
		uint32_t format)
{
	struct weston_compositor *compositor;
	struct gbm_buffer *buffer;
	uint32_t version;
	int i;

	compositor = wl_resource_get_user_data(backend_resource);

	buffer = zalloc(sizeof *buffer);
	if (!buffer)
		goto error;

	buffer->buffer_resource = wl_resource_create(client,
					&wl_buffer_interface,
					1, id);
	if (!buffer->buffer_resource)
		goto err_dealloc;

	buffer->compositor = compositor;
	buffer->fd = fd;
	buffer->metadata_fd = metadata_fd;
	buffer->width = width;
	buffer->height = height;
	buffer->format = format;

	wl_resource_set_implementation(buffer->buffer_resource,
				&gbm_buffer_implementation,
				buffer, destroy_gbm_buffer);

	return;

err_dealloc:
	free(buffer);
error:
	wl_resource_post_no_memory(backend_resource);
	return;

}

static void
gbm_buffer_backend_destroy(struct wl_client *client,
	struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct gbm_buffer_backend_interface
		gbm_buffer_backend_implementation = {
	gbm_buffer_backend_destroy,
	gbm_buffer_backend_create_buffer
};

static void
bind_gbm_buffer_backend(struct wl_client *client,
		  void *data, uint32_t version, uint32_t id)
{
	struct weston_compositor *compositor = data;
	struct wl_resource *resource;

	resource = wl_resource_create(client, &gbm_buffer_backend_interface,
					version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource,
			&gbm_buffer_backend_implementation,
			compositor, NULL);
}

WL_EXPORT int
gbm_buffer_backend_setup(struct weston_compositor *compositor)
{
	if (!wl_global_create(compositor->wl_display,
				&gbm_buffer_backend_interface, 1,
				compositor, bind_gbm_buffer_backend))
		return -1;

	return 0;
}

