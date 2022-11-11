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
* Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include <libweston/libweston.h>
#include "gbm-buffer-backend.h"
#include "gbm-buffer-backend-server-protocol.h"
#include "linux-dmabuf.h"
#include "linux-dmabuf-unstable-v1-server-protocol.h"
#include "gbm_priv.h"

struct early_renderer {
	struct weston_renderer base;

	struct gbm_device *gbm_hdle;
};

static bool
early_renderer_import_gbm_buffer(struct weston_compositor *ec,
		struct gbm_buffer *gbm_buf)
{
	struct early_renderer *er = (struct early_renderer*)ec->renderer;

	struct gbm_device * gbm = er->gbm_hdle;
	struct gbm_buf_info  buf_info;
	struct gbm_bo *bo;
	generic_buf_layout_t buf_lyt;
	uint32_t j = 0;

	buf_info.fd          = gbm_buf->fd;
	buf_info.metadata_fd = gbm_buf->metadata_fd;
	buf_info.height		 = gbm_buf->height;
	buf_info.width       = gbm_buf->width;
	buf_info.format      = gbm_buf->format;

	GBM_PROTOCOL_LOG(LOG_DBG,"early_renderer_import_gbm_buffer:Invoked");

	/* We will import BO to create an entry into the hash map for this buf_info */
	bo = gbm_bo_import(gbm, GBM_BO_IMPORT_GBM_BUF_TYPE, &buf_info, GBM_BO_USE_RENDERING);
	if (bo == NULL) {
		GBM_PROTOCOL_LOG(LOG_DBG,"failed to import gbm bo");
		return false;
	}

	/* save gbm buffer object */
	gbm_buf->bo = bo;

	GBM_PROTOCOL_LOG(LOG_DBG,"early_renderer_import_gbm_buffer:bo created= %p",bo);

	int ret=gbm_perform(GBM_PERFORM_GET_PLANE_INFO,bo,&buf_lyt);
	if(ret == GBM_ERROR_NONE){
		gbm_buf->num_planes = buf_lyt.num_planes;
		for(j = 0;j < buf_lyt.num_planes; j++){
			gbm_buf->offset[j] = buf_lyt.planes[j].offset;
			gbm_buf->stride[j] = buf_lyt.planes[j].v_increment;
		}
	} else {
		weston_log("gl_renderer_import_gbm_buffer::GET Plane Info failed\n");
		return false;
	}

	/* Fill up the remaining fields with default values */
	for(; j < MAX_NUM_PLANES; j++){
		gbm_buf->offset[j] = 0;
		gbm_buf->stride[j] = 0;
	}

	gbm_buffer_backend_set_user_data(gbm_buf, NULL, NULL);
	GBM_PROTOCOL_LOG(LOG_DBG,"gl_renderer_import_gbm_buffer:Invoke import_gbm_buffer()");

	return true;
}

static void
early_renderer_destroy(struct weston_compositor *ec){
	struct early_renderer *er = (struct early_renderer*)ec->renderer;

	free(er);
}

static void
early_renderer_surface_set_color(struct weston_surface *surface,
		float red, float green, float blue, float alpha)
{
	surface->surf_color.red = red;
	surface->surf_color.blue= blue;
	surface->surf_color.green = green;
	surface->surf_color.alpha = alpha;
	surface->surf_color.is_pended = true;
}

static void
early_renderer_flush_damage(struct weston_surface *surface)
{
}


static void
early_renderer_attach(struct weston_surface *es, struct weston_buffer *buffer)
{
	struct wl_shm_buffer *shm_buffer;
	struct gbm_buffer *gbmbuf;

	weston_log("early renderer attach\n");
	if (!buffer)
		return;

	/* this is for other renderer to determine which is the latter one, buffer or color
	 * of surface
	 * is_pended = false means the buffer is the latest one
	 * is_pended = true means the color is the latest one
	 */
	es->surf_color.is_pended = false;

	shm_buffer = wl_shm_buffer_get(buffer->resource);
	if (shm_buffer) {
		buffer->shm_buffer = shm_buffer;
		buffer->width = wl_shm_buffer_get_width(shm_buffer);
		buffer->height = wl_shm_buffer_get_height(shm_buffer);
	} else if ((gbmbuf = gbm_buffer_get(buffer->resource))){
		buffer->width = gbmbuf->width;
		buffer->height = gbmbuf->height;
		buffer->y_inverted =
			!!(gbmbuf->flags & ZWP_LINUX_BUFFER_PARAMS_V1_FLAGS_Y_INVERT);
	}
}

int early_renderer_init(struct weston_compositor *ec,
		struct gbm_device * gbm)
{
	struct early_renderer *renderer;

	renderer = zalloc(sizeof *renderer);
	if (renderer == NULL)
		return -1;

	renderer->base.read_pixels = NULL;
	renderer->base.repaint_output = NULL;
	renderer->base.flush_damage = early_renderer_flush_damage;
	renderer->base.attach = early_renderer_attach;
	renderer->base.surface_set_color = early_renderer_surface_set_color;
	renderer->base.destroy = early_renderer_destroy;
	renderer->base.surface_get_content_size = NULL;
	renderer->base.surface_copy_content = NULL;
	renderer->base.import_dmabuf     = NULL;
	renderer->base.import_gbm_buffer = early_renderer_import_gbm_buffer;
	renderer->gbm_hdle = gbm;
	ec->renderer = &renderer->base;

	return 0;
}
