/*
 * Copyright © 2008-2011 Kristian Høgsberg
 * Copyright © 2011 Intel Corporation
 * Copyright © 2017, 2018 Collabora, Ltd.
 * Copyright © 2017, 2018 General Electric Company
 * Copyright (c) 2018 DisplayLink (UK) Ltd.
 * Copyright (c) 2021 The Linux Foundation. All rights reserved.
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
 *
 * Changes from Qualcomm Innovation Center are provided under the following license:
 *
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "config.h"

#include <libweston/libweston.h>
#include <libweston/pixel-formats.h>

#include "sdm-internal.h"
#include "linux-dmabuf.h"
#include "presentation-time-server-protocol.h"

void
destroy_sdm_layer(struct sdm_layer *layer)
{
	pixman_region32_fini(&layer->overlap);
	weston_buffer_reference(&layer->buffer_ref, NULL);
	wl_list_remove(&layer->link);
	if (layer->fb) {
		drm_fb_unref(layer->fb);
	}
	free(layer);
}

static struct sdm_layer *
create_sdm_layer(struct drm_output *output, struct weston_view *ev,
		 pixman_region32_t *overlap, bool is_cursor, bool is_skip)
{
	struct sdm_layer *layer;

	layer = zalloc(sizeof(*layer));
	if (layer == NULL) {
	    return NULL;
	}

	layer->view = ev;
	layer->acquire_fence_fd = ev->surface->acquire_fence_fd;
	layer->is_cursor = is_cursor;
	layer->is_skip = is_skip;
	if (!is_skip)
		layer->fb = drm_fb_get_from_view(output, ev);

	pixman_region32_init(&layer->overlap);
	pixman_region32_copy(&layer->overlap, overlap);
	weston_buffer_reference(&layer->buffer_ref, ev->surface->buffer_ref.buffer);

	return layer;
}

void
drm_assign_planes(struct weston_output *output_base, void *repaint_data)
{
	struct drm_backend *b = to_drm_backend(output_base->compositor);
	struct drm_output *output = to_drm_output(output_base);
	struct weston_view *ev;
	struct weston_plane *primary = &output_base->compositor->primary_plane, *next_plane;
	struct sdm_layer *sdm_layer, *next_sdm_layer;
	bool is_skip = false;
	struct weston_surface *es;

	pixman_region32_t overlap, surface_overlap;

	pixman_region32_init(&overlap);

	output->view_count = 0;
	wl_list_init(&output->sdm_layer_list);
	wl_list_for_each(ev, &output_base->compositor->view_list, link) {

		/* If this view doesn't touch our output at all, there's no
		 * reason to do anything with it. */
		if (!(ev->output_mask & (1u << output->base.id)))
			continue;

		/* Test whether this buffer can ever go into a plane:
		 * non-shm, or small enough to be a cursor.
		 *
		 * Also, keep a reference when using the pixman renderer.
		 * That makes it possible to do a seamless switch to the GL
		 * renderer and since the pixman renderer keeps a reference
		 * to the buffer anyway, there is no side effects.
		 */
		if (b->use_pixman ||
		    (weston_view_has_valid_buffer(ev) &&
		    (!wl_shm_buffer_get(ev->surface->buffer_ref.buffer->resource) ||
		     (ev->surface->width <= b->cursor_width &&
		      ev->surface->height <= b->cursor_height))))
			ev->surface->keep_buffer = true;
		else
			ev->surface->keep_buffer = false;

		pixman_region32_init(&surface_overlap);
		pixman_region32_intersect(&surface_overlap, &overlap,
						&ev->transform.boundingbox);

		es = ev->surface;
		if (!es->buffer_ref.buffer) {
		    is_skip = true;
		} else if (linux_dmabuf_buffer_get(es->buffer_ref.buffer->resource)) {
		    is_skip = false;
		} else if (gbm_buffer_get(es->buffer_ref.buffer->resource)) {
                    is_skip = false;
		} else if (wl_shm_buffer_get(es->buffer_ref.buffer->resource)) {
		    is_skip = true;
		} else {
		    is_skip = true;
		}

		sdm_layer = create_sdm_layer(output, ev, &surface_overlap, false, is_skip);
		if (!sdm_layer)
			return;

		wl_list_insert(output->sdm_layer_list.prev, &sdm_layer->link);
		output->view_count++;
	}

	output->view_count++;

	int ret = Prepare(output->display_id, output);
	if (ret != 0) {
		weston_log("%s: Assigning planes failed\n", __func__);
		return;
	}

	pixman_region32_fini(&overlap);

	wl_list_for_each_safe(sdm_layer, next_sdm_layer, &output->sdm_layer_list, link) {
		next_plane = primary;
		ev = sdm_layer->view;
		/* Move to primary plane if Strategy set it to GPU composition */
		if (sdm_layer->composition_type == SDM_COMPOSITION_GPU) {
			weston_view_move_to_plane(ev, next_plane);
				pixman_region32_union(&overlap, &overlap, &ev->transform.boundingbox);
			ev->psf_flags = 0;
			destroy_sdm_layer(sdm_layer);
		} else {
			/* Composed by Display Hardware directly */
			/* ToDo(User): handle scenarios if SDE composition is not possible */
			ev->psf_flags = WP_PRESENTATION_FEEDBACK_KIND_ZERO_COPY;
			/* Set the view's plane back to NULL so that it is not composed by GPU */
			sdm_layer->view->plane = NULL;
		}
	}
}
