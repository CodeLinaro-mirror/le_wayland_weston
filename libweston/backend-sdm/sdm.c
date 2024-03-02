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
 * Copyright (c) 2022-2023, 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 */

#include <sys/eventfd.h>
#include <poll.h>
#include <gbm_priv.h>
#include "sdm-internal.h"
#include "shared/timespec-util.h"
#include "shared/string-helpers.h"
#include "pixman-renderer.h"
#include "pixel-formats.h"
#include "libinput-seat.h"
#include "launcher-util.h"
#include "presentation-time-server-protocol.h"
#include "linux-dmabuf.h"
#include "linux-dmabuf-unstable-v1-server-protocol.h"
#include "linux-explicit-synchronization.h"

#include "sdm-service/sdm_display_connect.h"


static const char default_seat[] = "seat0";
#define FENCE_TIMEOUT 1000

enum {
    PRIMARY_DISPLAY_ID,
    EXTERNAL_DISPLAY_ID
};

static int sync_wait(int fd, int timeout)
{
    struct pollfd fds;
    int ret;

    if (fd < 0) {
        errno = EINVAL;
        return -1;
    }

    fds.fd = fd;
    fds.events = POLLIN;

    do {
        ret = poll(&fds, 1, timeout);
        if (ret > 0) {
            if (fds.revents & (POLLERR | POLLNVAL)) {
                errno = EINVAL;
                return -1;
            }
            return 0;
        } else if (ret == 0) {
            errno = ETIME;
            return -1;
        }
    } while (ret == -1 && (errno == EINTR || errno == EAGAIN));

    return ret;
}

static void
vblank_handler(int display_id, int64_t timestamp, void *data)
{
	struct drm_output *output = (struct drm_output *) data;
	uint64_t v = 1;

	output->last_vblank.sec = timestamp / 1000000000LL;
	output->last_vblank.usec = (timestamp % 1000000000LL) / 1000;

	write(output->vblank_ev_fd, &v, sizeof v);
}

static void
hotplug_handler(int disp, bool connected, void *data)
{
       weston_log("Hotplug connected = %d called\n", connected);
}

static int
on_vblank(int fd, uint32_t mask, void *data)
{
	struct drm_output *output = (struct drm_output *) data;
	/* During the initial modeset, we can disable CRTCs which we don't
	 * actually handle during normal operation; this will give us events
	 * for unknown outputs. Ignore them. */
	if (!output || !output->base.enabled)
		return -1;

	struct drm_backend *b = to_drm_backend(output->base.compositor);;
	unsigned int sec, usec;
	uint64_t v = 0;

	read(fd, &v, sizeof(v));

	uint32_t flags = WP_PRESENTATION_FEEDBACK_KIND_VSYNC |
			 WP_PRESENTATION_FEEDBACK_KIND_HW_COMPLETION |
			 WP_PRESENTATION_FEEDBACK_KIND_HW_CLOCK;

	if (output->atomic_complete_pending) {
		drm_output_update_msc(output, output->last_vblank.frame);
		output->atomic_complete_pending = false;

		assert(b->atomic_modeset);

		usec = output->last_vblank.usec;
		sec = output->last_vblank.sec;
		drm_output_update_complete(output, flags, sec, usec);
	}
	return 0;
}

void
sdm_weston_transformed_rect(struct drm_output *output,
				 pixman_box32_t *tbox, pixman_box32_t box)
{
	*tbox = weston_transformed_rect(output->base.width,
					output->base.height,
					output->base.transform,
					output->base.current_scale,
					box);
}

void
sdm_weston_global_transform_rect(struct weston_view *ev,
				pixman_box32_t *box, float *x1, float *y1,
				float *x2, float *y2)
{
	float sx1, sy1, sx2, sy2;
	weston_view_from_global_float(ev, box->x1, box->y1, &sx1, &sy1);
	weston_surface_to_buffer_float(ev->surface, sx1, sy1, &sx1, &sy1);
	weston_view_from_global_float(ev, box->x2, box->y2, &sx2, &sy2);
	weston_surface_to_buffer_float(ev->surface, sx2, sy2, &sx2, &sy2);

	*x1 = sx1;
	*y1 = sy1;
	*x2 = sx2;
	*y2 = sy2;
}

static int
drm_output_enable_vblank(struct drm_output *output)
{
     struct wl_event_loop *loop;

     output->vblank_ev_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
     if (output->vblank_ev_fd < 0)
        return -1;

     loop = wl_display_get_event_loop(output->base.compositor->wl_display);
     output->vblank_ev_source =
		 wl_event_loop_add_fd(loop, output->vblank_ev_fd,
					WL_EVENT_READABLE, on_vblank, output);
     return 0;
}

static void
drm_output_disable_vblank(struct drm_output *output)
{
	if (output->vblank_ev_source != NULL) {
	        wl_event_source_remove(output->vblank_ev_source);
	        output->vblank_ev_source = NULL;
	}

	if (output->vblank_ev_fd != -1) {
		close(output->vblank_ev_fd);
		output->vblank_ev_fd = -1;
	}

	SetVSyncState(output->display_id, false, output);
}

static int
pageflip_timeout(void *data) {
	/*
	 * Our timer just went off, that means we're not receiving drm
	 * page flip events anymore for that output. Let's gracefully exit
	 * weston with a return value so devs can debug what's going on.
	 */
	struct drm_output *output = data;
	struct weston_compositor *compositor = output->base.compositor;

	weston_log("Pageflip timeout reached on output %s, your "
	           "driver is probably buggy!  Exiting.\n",
		   output->base.name);
	weston_compositor_exit_with_code(compositor, EXIT_FAILURE);

	return 0;
}

/* Creates the pageflip timer. Note that it isn't armed by default */
static int
drm_output_pageflip_timer_create(struct drm_output *output)
{
	struct wl_event_loop *loop = NULL;
	struct weston_compositor *ec = output->base.compositor;

	loop = wl_display_get_event_loop(ec->wl_display);
	assert(loop);
	output->pageflip_timer = wl_event_loop_add_timer(loop,
	                                                 pageflip_timeout,
	                                                 output);

	if (output->pageflip_timer == NULL) {
		weston_log("creating drm pageflip timer failed: %s\n",
			   strerror(errno));
		return -1;
	}

	return 0;
}

static void
drm_output_destroy(struct weston_output *output_base);

/**
 * Mark a drm_output_state (the output's last state) as complete. This handles
 * any post-completion actions such as updating the repaint timer, disabling the
 * output, and finally freeing the state.
 */
void
drm_output_update_complete(struct drm_output *output, uint32_t flags,
			   unsigned int sec, unsigned int usec)
{
	struct timespec ts;
	struct sdm_layer *sdm_layer, *tmp_layer;

	/* Stop the pageflip timer instead of rearming it here */
	if (output->pageflip_timer)
		wl_event_source_timer_update(output->pageflip_timer, 0);

	drm_fb_unref(output->next_fb);
	output->next_fb = NULL;

	wl_list_for_each_safe(sdm_layer, tmp_layer, &output->sdm_layer_list, link) {
		destroy_sdm_layer(sdm_layer);
	}

	wl_list_init(&output->sdm_layer_list);

	if (output->dpms != WESTON_DPMS_ON) {
		if (output->destroy_pending) {
			output->destroy_pending = false;
			output->disable_pending = false;
			output->dpms_off_pending = false;
			drm_output_destroy(&output->base);
			return;
		} else if (output->disable_pending) {
			output->disable_pending = false;
			output->dpms_off_pending = false;
			weston_output_disable(&output->base);
			return;
		} else if (output->dpms_off_pending) {
			output->dpms_off_pending = false;
		}
	}

	ts.tv_sec = sec;
	ts.tv_nsec = usec * 1000;

	if (output->base.repaint_status == REPAINT_AWAITING_COMPLETION)
		weston_output_finish_frame(&output->base, &ts, flags);

}

static struct drm_fb *
drm_output_render_pixman(struct drm_output *output,
			 pixman_region32_t *damage)
{
	struct weston_compositor *ec = output->base.compositor;
	struct drm_backend *b = to_drm_backend(ec);

	output->current_image ^= 1;

	pixman_renderer_output_set_buffer(&output->base,
					  output->image[output->current_image]);
	pixman_renderer_output_set_hw_extra_damage(&output->base,
						   &output->previous_damage);

	ec->renderer->repaint_output(&output->base, damage);

	pixman_region32_copy(&output->previous_damage, damage);

	return drm_fb_get_from_bo(output->dumb_bo[output->current_image],
						b, true, BUFFER_PIXMAN_GBM);
}

static void
drm_output_render(struct drm_output *output, pixman_region32_t *damage)
{
	struct weston_compositor *c = output->base.compositor;
	struct drm_backend *b = to_drm_backend(c);
	struct drm_fb *fb;

	if (b->use_pixman) {
		fb = drm_output_render_pixman(output, damage);
	} else {
		fb = drm_output_render_gl(output, damage);
	}

	if (!fb) {
		output->next_fb = NULL;
		weston_log("ERROR: No repaint framebuffer, \
				output=%s expect crashs\n", output->base.name);
		return;
	}

	output->next_fb = fb;
	pixman_region32_subtract(&c->primary_plane.damage,
				 &c->primary_plane.damage, damage);
}

/* returns a value between 0-255 range, where higher is brighter */
static uint32_t
drm_get_backlight(int disp_id)
{
	float brightness = -1.0f;
	uint32_t level = 0;
	int ret = 0;

	ret = GetPanelBrightness(disp_id, &brightness);
	if (ret) {
		weston_log("%s: failed error=%d\n", __func__, ret);
		return ret;
	}

	if (brightness == -1.0f) {
		level = 0;
	} else {
		level = (uint32_t)(254.0f*brightness + 1);
	}

	weston_log("%s: backlight value:%d brightness:%f \n", __func__, level, brightness);
	return level;
}

/* values accepted are between 0-255 range */
static int
drm_set_backlight(struct weston_output *output_base, uint32_t value)
{
	int ret = 0;
	struct drm_output *output = to_drm_output(output_base);
	int disp_id = output->display_id;

	if (!(0 <= value && value <= 255)) {
		weston_log("%s: not in supported range, backlight = %d\n", __func__, value);
		return -1;
	}

	if (value == drm_get_backlight(disp_id)) {
		weston_log("%s: already in same state, backlight = %d\n", __func__, value);
		return 0;
	}

	if (value == 0) {
		ret = SetPanelBrightness(disp_id, -1.0f);
	} else {
		ret = SetPanelBrightness(disp_id, (value - 1)/254.0f);
	}

	if (ret) {
		weston_log("%s: backlight setting failed error = %d\n", __func__, ret);
		return ret;
	}
	output_base->backlight_current = drm_get_backlight(disp_id);

	weston_log("%s: backlight set to value: %d \n", __func__, value);
	return ret;
}

static int
drm_output_repaint(struct weston_output *output_base,
		   pixman_region32_t *damage,
		   void *repaint_data)
{
	struct drm_output *output = to_drm_output(output_base);

	if (output->disable_pending || output->destroy_pending)
		return -1;

	if (output->next_fb)
		return 0;

	drm_output_render(output, damage);
	if (!output->next_fb) {
		weston_log("error: framebuffer not created\n");
		return -1;
	}

	return 0;
}

static int
drm_output_start_repaint_loop(struct weston_output *output_base)
{
	struct drm_output *output = to_drm_output(output_base);

	if (output->disable_pending || output->destroy_pending)
		return 0;

	//SDM will handle vblank. Finish frame immediately.
	weston_output_finish_frame(output_base, NULL,
				   WP_PRESENTATION_FEEDBACK_INVALID);
	return 0;
}

/**
 * Begin a new repaint cycle
 *
 * Called by the core compositor at the beginning of a repaint cycle. Creates
 * a new pending_state structure to own any output state created by individual
 * output repaint functions until the repaint is flushed or cancelled.
 */
static void *
drm_repaint_begin(struct weston_compositor *compositor)
{
	struct drm_backend *b = to_drm_backend(compositor);
	struct drm_pending_state *ret = NULL;

	b->repaint_data = NULL;

	if (weston_log_scope_is_enabled(b->debug)) {
		char *dbg = weston_compositor_print_scene_graph(compositor);
		drm_debug(b, "[repaint] Beginning repaint; pending_state %p\n",
			  ret);
		drm_debug(b, "%s", dbg);
		free(dbg);
	}

	return ret;
}

/**
 * Flush a repaint set
 *
 * Called by the core compositor when a repaint cycle has been completed
 * and should be flushed. Frees the pending state, transitioning ownership
 * of the output state from the pending state, to the update itself. When
 * the update completes (see drm_output_update_complete), the output
 * state will be freed.
 */
static int
drm_repaint_flush(struct weston_compositor *compositor, void *repaint_data)
{
	struct drm_backend *b = to_drm_backend(compositor);
	struct weston_output *output = NULL;
	int ret = 0;

	wl_list_for_each(output, &compositor->output_list, link) {
		struct drm_output *drm_output = to_drm_output(output);

		if (!drm_output->next_fb)
			continue;

		ret = SetVSyncState(drm_output->display_id, true, drm_output);

		if (ret != 0) {
			weston_log("Vsync failed\n");
			return -1;
		}

		ret = Commit(drm_output->display_id, drm_output);

		if (ret != 0) {
			weston_log("%s : commit failed err = %d\n", __func__, ret);
			return -2;
		}

		drm_output->atomic_complete_pending = true;
	}

	b->repaint_data = NULL;

	return 0;
}

/**
 * Cancel a repaint set
 *
 * Called by the core compositor when a repaint has finished, so the data
 * held across the repaint cycle should be discarded.
 */
static void
drm_repaint_cancel(struct weston_compositor *compositor, void *repaint_data)
{
	struct drm_backend *b = to_drm_backend(compositor);

	if (b->repaint_data) {
		free(b->repaint_data);
		b->repaint_data = NULL;
	}
}

static int
drm_output_init_pixman(struct drm_output *output, struct drm_backend *b);
static void
drm_output_fini_pixman(struct drm_output *output);

static int
drm_output_switch_mode(struct weston_output *output_base, struct weston_mode *mode)
{
	struct drm_output *output = to_drm_output(output_base);
	struct drm_backend *b = to_drm_backend(output_base->compositor);
	struct drm_mode *drm_mode = drm_output_choose_mode(output, mode);

	if (!drm_mode) {
		weston_log("%s: invalid resolution %dx%d\n",
			   output_base->name, mode->width, mode->height);
		return -1;
	}

	if (&drm_mode->base == output->base.current_mode)
		return 0;

	output->base.current_mode->flags = 0;

	output->base.current_mode = &drm_mode->base;
	output->base.current_mode->flags =
		WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED;

	/* XXX: This drops our current buffer too early, before we've started
	 *      displaying it. Ideally this should be much more atomic and
	 *      integrated with a full repaint cycle, rather than doing a
	 *      sledgehammer modeswitch first, and only later showing new
	 *      content.
	 */
	b->state_invalid = true;

	if (b->use_pixman) {
		drm_output_fini_pixman(output);
		if (drm_output_init_pixman(output, b) < 0) {
			weston_log("failed to init output pixman state with "
				   "new mode\n");
			return -1;
		}
	} else {
		drm_output_fini_egl(output);
		if (drm_output_init_egl(output, b) < 0) {
			weston_log("failed to init output egl state with "
				   "new mode");
			return -1;
		}
	}

	return 0;
}

static struct gbm_device *
create_gbm_device(int fd)
{
	struct gbm_device *gbm;

	gbm = gbm_create_device(fd);

	return gbm;
}

static int
init_pixman(struct drm_backend *b)
{
	b->gbm = create_gbm_device(b->drm.fd);

	if (!b->gbm)
		return -1;

	return pixman_renderer_init(b->compositor);
}

/**
 * Power output on or off
 *
 * The DPMS/power level of an output is used to switch it on or off. This
 * is DRM's hook for doing so, which can called either as part of repaint,
 * or independently of the repaint loop.
 *
 * If we are called as part of repaint, we simply set the relevant bit in
 * state and return.
 *
 * This function is never called on a virtual output.
 */
static void
drm_set_dpms(struct weston_output *output_base, enum dpms_enum level)
{
	struct drm_output *output = to_drm_output(output_base);
	int ret;

	weston_log("%s: SDM DPMS level = %d\n", __func__, level);
	ret = SetDisplayState(output->display_id, level);

	if (ret) {
		weston_log("%s: SDM DPMS ON failed error = %d\n", __func__, ret);
		return ret;
	}

	output->dpms = level;
	/* As we throw everything away when disabling, just send us back through
	 * a repaint cycle. */
	if (level == WESTON_DPMS_ON) {
		if (output->dpms_off_pending)
			output->dpms_off_pending = false;
		weston_output_schedule_repaint(output_base);
		return;
	}
}

static int
drm_output_init_pixman(struct drm_output *output, struct drm_backend *b)
{
	int w = output->base.current_mode->width;
	int h = output->base.current_mode->height;
	uint32_t format = output->gbm_format;
	uint32_t pixman_format;
	unsigned int i;
	const struct pixman_renderer_output_options options = {
		.use_shadow = b->use_pixman_shadow,
		.gbm_handle = b->gbm
	};

	switch (format) {
		case GBM_FORMAT_ABGR8888:
			pixman_format = PIXMAN_a8b8g8r8;
			break;
		case GBM_FORMAT_ARGB8888:
			pixman_format = PIXMAN_a8r8g8b8;
			break;
		case GBM_FORMAT_XRGB8888:
			pixman_format = PIXMAN_x8r8g8b8;
			break;
		case GBM_FORMAT_RGB565:
			pixman_format = PIXMAN_r5g6b5;
			break;
		default:
			weston_log("Unsupported pixman format 0x%x\n", format);
			return -1;
	}

	/* FIXME error checking */
	for (i = 0; i < ARRAY_LENGTH(output->dumb_bo); i++) {
		output->dumb_bo[i] = gbm_bo_create(b->gbm, w, h, format, output->gbm_bo_flags);
		if (!output->dumb_bo[i])
			goto err;

		// Use gbm buffer as pixman image
		uint8_t *bo_mmap = NULL;
		int ret = -1;
		ret = gbm_perform(GBM_PERFORM_CPU_MAP_FOR_BO, output->dumb_bo[i], &bo_mmap);
		if (ret != GBM_ERROR_NONE) {
			weston_log("gbm bo map failed with ret = %d\n", ret);
			goto err;
		}

		weston_log("gbm bo [%d] map = %p\n", i, bo_mmap);

		output->image[i] =
		pixman_image_create_bits(pixman_format, w, h,
						 bo_mmap, gbm_bo_get_stride(output->dumb_bo[i]));
		if (!output->image[i])
			goto err;
	}

	if (pixman_renderer_output_create(&output->base, &options) < 0)
		goto err;

	weston_log("DRM: output %s %s shadow framebuffer.\n", output->base.name,
		   b->use_pixman_shadow ? "uses" : "does not use");

	pixman_region32_init_rect(&output->previous_damage,
				  output->base.x, output->base.y, output->base.width, output->base.height);

	return 0;

err:
	for (i = 0; i < ARRAY_LENGTH(output->dumb_bo); i++) {
		if (output->image[i])
			pixman_image_unref(output->image[i]);
		if (output->dumb_bo[i])
			gbm_bo_destroy(output->dumb_bo[i]);

		output->image[i] = NULL;
		output->dumb_bo[i] = NULL;
	}

	return -1;
}

static void
drm_output_fini_pixman(struct drm_output *output)
{
	unsigned int i;

	pixman_renderer_output_destroy(&output->base);
	pixman_region32_fini(&output->previous_damage);

	for (i = 0; i < ARRAY_LENGTH(output->dumb_bo); i++) {
		pixman_image_unref(output->image[i]);
		output->image[i] = NULL;
		gbm_bo_destroy(output->dumb_bo[i]);
		output->dumb_bo[i] = NULL;
	}
}

static void
setup_output_seat_constraint(struct drm_backend *b,
			     struct weston_output *output,
			     const char *s)
{
	if (strcmp(s, "") != 0) {
		struct weston_pointer *pointer;
		struct udev_seat *seat;

		seat = udev_seat_get_named(&b->input, s);
		if (!seat)
			return;

		seat->base.output = output;

		pointer = weston_seat_get_pointer(&seat->base);
		if (pointer)
			weston_pointer_clamp(pointer,
					     &pointer->x,
					     &pointer->y);
	}
}

static int
drm_output_attach_head(struct weston_output *output_base,
		       struct weston_head *head_base)
{
	struct drm_backend *b = to_drm_backend(output_base->compositor);

	if (wl_list_length(&output_base->head_list) >= MAX_CLONED_CONNECTORS)
		return -1;

	if (!output_base->enabled)
		return 0;

	/* XXX: ensure the configuration will work.
	 * This is actually impossible without major infrastructure
	 * work. */

	/* Need to go through modeset to add connectors. */
	/* XXX: Ideally we'd do this per-output, not globally. */
	/* XXX: Doing it globally, what guarantees another output's update
	 * will not clear the flag before this output is updated?
	 */
	b->state_invalid = true;

	weston_output_schedule_repaint(output_base);

	return 0;
}

static void
drm_output_detach_head(struct weston_output *output_base,
		       struct weston_head *head_base)
{
	struct drm_backend *b = to_drm_backend(output_base->compositor);

	if (!output_base->enabled)
		return;

	/* Need to go through modeset to drop connectors that should no longer
	 * be driven. */
	/* XXX: Ideally we'd do this per-output, not globally. */
	b->state_invalid = true;

	weston_output_schedule_repaint(output_base);
}

int
parse_gbm_format(const char *s, uint32_t default_value, uint32_t *gbm_format)
{
	const struct pixel_format_info *pinfo;

	if (s == NULL) {
		*gbm_format = default_value;

		return 0;
	}

	pinfo = pixel_format_get_info_by_drm_name(s);
	if (!pinfo) {
		weston_log("fatal: unrecognized pixel format: %s\n", s);

		return -1;
	}

	/* GBM formats and DRM formats are identical. */
	*gbm_format = pinfo->format;

	return 0;
}

static void
drm_output_set_gbm_format(struct weston_output *base,
			  const char *gbm_format)
{
	struct drm_output *output = to_drm_output(base);
	struct drm_backend *b = to_drm_backend(base->compositor);

	if (parse_gbm_format(gbm_format, b->gbm_format, &output->gbm_format) == -1)
		output->gbm_format = b->gbm_format;
}

static void
drm_output_set_seat(struct weston_output *base,
		    const char *seat)
{
	struct drm_output *output = to_drm_output(base);
	struct drm_backend *b = to_drm_backend(base->compositor);

	setup_output_seat_constraint(b, &output->base,
				     seat ? seat : "");
}

static int
drm_output_enable(struct weston_output *base)
{
	struct drm_output *output = to_drm_output(base);
	struct drm_backend *b = to_drm_backend(base->compositor);

	if (b->pageflip_timeout) {
		drm_output_pageflip_timer_create(output);
	}

	if (b->use_pixman) {
		if (drm_output_init_pixman(output, b) < 0) {
			weston_log("Failed to init output pixman state\n");
			return -1;
		}
	} else if (drm_output_init_egl(output, b) < 0) {
		weston_log("Failed to init output gl state\n");
		return -1;
	}

	output->base.start_repaint_loop = drm_output_start_repaint_loop;
	output->base.repaint = drm_output_repaint;
	output->base.assign_planes = drm_assign_planes;
	output->base.set_dpms = drm_set_dpms;
	output->base.switch_mode = drm_output_switch_mode;
	output->base.set_backlight = drm_set_backlight;
	output->base.backlight_current = drm_get_backlight(output->display_id);
	output->base.set_gamma = NULL;

	if (drm_output_enable_vblank(output)) {
		weston_log("Failed to create vblank event\n");
		return -1;
	}

	drm_output_print_modes(output);

       // weston treats 255 as max brightness setting initial brightness to ~50% on builtin display

        uint32_t disp_id = output->display_id;
        int type = GetDisplayType(disp_id);
        if (type == 0) {
               int ret = drm_set_backlight(base, 127);
               if (ret != 0) {
                       weston_log("Failed to Init backlight\n");
                       return -1;
                }
       }

	return 0;
}

static void
drm_output_deinit(struct weston_output *base)
{
	struct drm_output *output = to_drm_output(base);
	struct drm_backend *b = to_drm_backend(base->compositor);

	if (b->use_pixman)
		drm_output_fini_pixman(output);
	else
		drm_output_fini_egl(output);
}

static void
drm_head_output_power_off(struct drm_head *head);

static void
drm_head_destroy(struct drm_head *head);

static void
drm_output_destroy(struct weston_output *base)
{
	struct drm_output *output = to_drm_output(base);
	struct drm_backend *b = to_drm_backend(base->compositor);

	if (output->page_flip_pending || output->atomic_complete_pending) {
		output->destroy_pending = true;
		weston_log("destroy output while page flip pending\n");
		return;
	}

	if (output->base.enabled)
		drm_output_deinit(&output->base);

	drm_mode_list_destroy(b, &output->base.mode_list);

	if (output->pageflip_timer)
		wl_event_source_remove(output->pageflip_timer);

	drm_output_disable_vblank(output);
	weston_output_release(&output->base);

	free(output);
}

static int
drm_output_disable(struct weston_output *base)
{
	struct drm_output *output = to_drm_output(base);

	if (output->page_flip_pending || output->atomic_complete_pending) {
		output->disable_pending = true;
		return -1;
	}

	weston_log("Disabling output %s\n", output->base.name);

	if (output->base.enabled)
		drm_output_deinit(&output->base);

	output->disable_pending = false;

	SetVSyncState(output->display_id, false, output);

	return 0;
}

static void
drm_head_log_info(struct drm_head *head, const char *msg)
{
	if (head->base.connected) {
		weston_log("DRM: head '%s' %s, connector %d is connected, "
			   "EDID make '%s', model '%s', serial '%s'\n",
			   head->base.name, msg, head->connector_id,
			   head->base.make, head->base.model,
			   head->base.serial_number ?: "");
	} else {
		weston_log("DRM: head '%s' %s, connector %d is disconnected.\n",
			   head->base.name, msg, head->connector_id);
	}
}

/**
 * Create a Weston head for a connector
 *
 * Given a DRM connector, create a matching drm_head structure and add it
 * to Weston's head list.
 *
 * @param backend Weston backend structure
 * @param connector_id DRM connector ID for the head
 * @param drm_device udev device pointer
 * @returns The new head, or NULL on failure.
 */
static struct drm_head *
drm_head_create(struct drm_backend *backend, struct udev_device *drm_device, int display_id)
{
	struct drm_head *head;
	char *name;
	const char *make = "unknown";
	const char *model = "unknown";
	const char *serial_number = "unknown";
	int width = 0, height = 0, refresh = 0;

	if (!IsDisplayCreated(display_id)) {
		sdm_cbs_t sdm_cbs;
		int rc = CreateDisplay(display_id);
		if (rc != 0) {
			weston_log("SDM failed to create display id: %d, error = %d\n",
						display_id, rc);
			return NULL;
		} else {
			weston_log("%s: SDM display (id: %d) created\n", __func__, display_id);
		}

		/* Now register callbacks with SDM services */
		sdm_cbs.vblank_cb = vblank_handler;
		sdm_cbs.hotplug_cb = hotplug_handler;
		RegisterCbs(display_id, &sdm_cbs);
	} else {
		weston_log("%s: SDM display created already\n", __func__);
	}

	head = zalloc(sizeof *head);
	if (!head)
		return NULL;

	name = GetConnectorName(display_id);
	if (!name)
		goto err_alloc;
	weston_log("display_id:%d and connector name:%s\n",display_id, name);

	weston_head_init(&head->base, name);

	free(name);

	head->connector_id = display_id;
	head->backend = backend;

	weston_head_set_monitor_strings(&head->base, make, model, serial_number);
	weston_head_set_non_desktop(&head->base, false);
	weston_head_set_subpixel(&head->base, WL_OUTPUT_SUBPIXEL_UNKNOWN);

	struct DisplayConfigInfo display_config;
	display_config.x_pixels        = 0;
	display_config.y_pixels        = 0;
	display_config.x_dpi           = 96.0f;
	display_config.y_dpi           = 96.0f;
	display_config.fps             = 0;
	display_config.vsync_period_ns = 0;
	display_config.is_yuv          = false;

	bool rc = GetDisplayConfiguration(display_id, &display_config);

	if (rc != 0) {
		width   = display_config.x_pixels;
		height  = display_config.y_pixels;
		refresh = display_config.fps*1000;
		weston_log("Display configuration w*h:%d %d\n", width, height);
	} else { /* default 1080p, 60 fps */
		weston_log("Fail to get preferred mode, use default mode instead!\n");
		width   = 1920;
		height  = 1080;
		refresh = 60*1000;
	}

	weston_head_set_physical_size(&head->base, width, height);

	weston_head_set_connection_status(&head->base, true);

	/* Disable HDCP for now */
	weston_head_set_content_protection_status(&head->base,
							 WESTON_HDCP_DISABLE);
	weston_head_set_internal(&head->base);
	weston_compositor_add_head(backend->compositor, &head->base);

	drm_head_log_info(head, "found");

	return head;

err_alloc:
	free(head);

	return NULL;
}

static void
drm_head_output_power_off(struct drm_head *head) {
	struct weston_output *output_base = weston_head_get_output(&head->base);
	if (output_base) {
		drm_set_dpms(output_base, WESTON_DPMS_OFF);
	}
}

static void
drm_head_destroy(struct drm_head *head)
{
	weston_head_release(&head->base);
	if (DestroyDisplay(head->connector_id)) {
		weston_log("DestroyDisplay: failed for head '%s' (connector %d).\n",
					head->base.name, head->connector_id);
	}
	free(head);
}

/**
 * Create a Weston output structure
 *
 * Create an "empty" drm_output. This is the implementation of
 * weston_backend::create_output.
 *
 * Creating an output is usually followed by drm_output_attach_head()
 * and drm_output_enable() to make use of it.
 *
 * @param compositor The compositor instance.
 * @param name Name for the new output.
 * @returns The output, or NULL on failure.
 */
static struct weston_output *
drm_output_create(struct weston_compositor *compositor, const char *name)
{
	struct drm_backend *b = to_drm_backend(compositor);
	struct drm_output *output;

	output = zalloc(sizeof *output);
	if (output == NULL)
		return NULL;

	output->backend = b;
#ifdef BUILD_DRM_GBM
	output->gbm_bo_flags = GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING;
#endif

	weston_output_init(&output->base, compositor, name);

	wl_list_init(&output->sdm_layer_list);

	output->base.enable = drm_output_enable;
	output->base.destroy = drm_output_destroy;
	output->base.disable = drm_output_disable;
	output->base.attach_head = drm_output_attach_head;
	output->base.detach_head = drm_output_detach_head;

	output->destroy_pending = false;
	output->disable_pending = false;

	weston_compositor_add_pending_output(&output->base, b->compositor);

	return &output->base;
}

struct drm_head *
drm_head_find_by_connector(struct drm_backend *backend, uint32_t connector_id)
{
	struct weston_head *base;
	struct drm_head *head;

	wl_list_for_each(base,
			 &backend->compositor->head_list, compositor_link) {
		head = to_drm_head(base);
		if (head->connector_id == connector_id)
			return head;
	}

	return NULL;
}

static int
drm_backend_create_heads(struct drm_backend *b, struct udev_device *drm_device)
{
	struct drm_head *head;
	int i = 0, rc = 0, num_displays;
	uint32_t *display_ids = NULL;

	rc = GetDisplayInfos();
	if (rc) {
		weston_log("Failed to get display information from SDM!\n");
		return -1;
	}

	// Collect connectors list
	num_displays = GetDisplayCount();
	display_ids = calloc(num_displays, sizeof(uint32_t));
	if (!display_ids) {
		return -1;
	}
	rc = GetConnectedDisplaysIds(num_displays, display_ids);
	if (rc) {
		goto err_connector;
	}

	/* collect new connectors that have appeared, e.g. MST */
	for (i = 0; i < num_displays; i++) {
		uint32_t display_id = display_ids[i];

		head = drm_head_find_by_connector(b, display_id);
		if (head) {
			weston_log("Head (with display_id: %d) already available.\n", display_id);
		} else {
			head = drm_head_create(b, drm_device, display_id);
			if (!head) {
				weston_log("DRM: failed to create head for hot-added"
						   " connector %d.\n", display_id);
				continue;
			}
		}

		rc = SetDisplayState(display_id, WESTON_DPMS_ON);
		if (rc) {
			weston_log("%s: SDM failed to turn on display (%d), error=%d\n",
					   __func__, display_id, rc);
			goto err_connector;
		}
	}

err_connector:
	free(display_ids);
	return rc;
}

static int
udev_event_is_disconnected(struct drm_backend *b, struct udev_device *device) {
    const char *status = udev_device_get_property_value(device, "status");

    if (!status)
        return 0;

    return strcmp(status, "disconnected") == 0;
}

static int
udev_event_is_connected(struct drm_backend *b, struct udev_device *device) {
    const char *status = udev_device_get_property_value(device, "status");

    if (status == NULL)
        return 0;

    return strcmp(status, "connected") == 0;
}

static void
drm_backend_update_heads(struct drm_backend *b, struct udev_device *drm_device)
{
	struct weston_head *base, *next;
	struct drm_head *head;
	int i, rc = 0, num_displays = 0;
	uint32_t *display_ids = NULL;

	rc = GetDisplayInfos();
	if (rc) {
		weston_log("Failed to get display information from SDM!\n");
		return;
	}

	// Collect connectors list
	num_displays = GetDisplayCount();
	display_ids = calloc(num_displays, sizeof(uint32_t));
	if (!display_ids) {
		return;
	}
	rc = GetConnectedDisplaysIds(num_displays, display_ids);
	if (rc) {
		goto err_connector;
	}

	/* collect new connectors that have appeared, e.g. MST */
	for (i = 0; i < num_displays; i++) {
		uint32_t display_id = display_ids[i];

		head = drm_head_find_by_connector(b, display_id);
		if (head) {
			weston_log("Head (with display_id: %d) already available.\n", display_id);
			// TODO: Implement functionality to update display config
			// drm_head_update_info(head);
		} else {
			head = drm_head_create(b, drm_device, display_id);
			if (!head) {
				weston_log("DRM: failed to create head for hot-added"
						   " connector %d.\n", display_id);
				continue;
			}
		}
		SetDisplayState(display_id, WESTON_DPMS_ON);
		weston_log("%s: CONNECT done for display_id: %d\n", __func__, display_id);
	}

	/* Remove connectors that have disappeared. */
	wl_list_for_each_safe(base, next, &b->compositor->head_list, compositor_link) {
		bool removed = true;

		head = to_drm_head(base);

		for (i = 0; i < num_displays; i++) {
			if (display_ids[i] == head->connector_id) {
				removed = false;
				break;
			}
		}

		if (!removed)
			continue;

		weston_log("DRM: head '%s' (connector %d) disappeared.\n",
			   head->base.name, head->connector_id);
		drm_head_output_power_off(head);
		drm_head_destroy(head);
	}

err_connector:
	free(display_ids);
}

static int
udev_event_is_hotplug(struct drm_backend *b, struct udev_device *device)
{
	const char *sysnum;
	const char *val;

	sysnum = udev_device_get_sysnum(device);
	if (!sysnum || atoi(sysnum) != b->drm.id)
		return 0;

	val = udev_device_get_property_value(device, "ACTION");
	if (!val)
		return 0;

	return strcmp(val, "change") == 0;
}

static int
udev_drm_event(int fd, uint32_t mask, void *data)
{
	struct drm_backend *b = data;
	struct udev_device *event = udev_monitor_receive_device(b->udev_monitor);;
	int hpd = udev_event_is_hotplug(b, event);
	int connected = udev_event_is_connected(b, event);
	int disconnected = udev_event_is_disconnected(b, event);
	int expect_hpd = hpd && (connected || disconnected);

	if (expect_hpd) {
		weston_log("expected hpd event,connected=[%d] disconnected=[%d]\n",
				connected, disconnected);
		drm_backend_update_heads(b, event);
	}

	udev_device_unref(event);

	return 1;
}

static void
drm_destroy(struct weston_compositor *ec)
{
	struct drm_backend *b = to_drm_backend(ec);
	struct weston_head *base, *next;

	udev_input_destroy(&b->input);

	wl_event_source_remove(b->udev_drm_source);
	//wl_event_source_remove(b->drm_source);

	b->shutting_down = true;

	weston_log_scope_destroy(b->debug);
	b->debug = NULL;
	weston_compositor_shutdown(ec);


	wl_list_for_each_safe(base, next, &ec->head_list, compositor_link)
		drm_head_destroy(to_drm_head(base));

	DestroyCore();

#ifdef BUILD_DRM_GBM
	if (b->gbm)
		gbm_device_destroy(b->gbm);
#endif

	udev_monitor_unref(b->udev_monitor);
	udev_unref(b->udev);

	weston_launcher_destroy(ec->launcher);

	wl_array_release(&b->unused_crtcs);

	close(b->drm.fd);
	free(b->drm.filename);
	free(b);
}

static void
session_notify(struct wl_listener *listener, void *data)
{
	struct weston_compositor *compositor = data;
	struct drm_backend *b = to_drm_backend(compositor);

	if (compositor->session_active) {
		weston_log("activating session\n");
		weston_compositor_wake(compositor);
		weston_compositor_damage_all(compositor);
		b->state_invalid = true;
		udev_input_enable(&b->input);
	} else {
		weston_log("deactivating session\n");
		udev_input_disable(&b->input);

		weston_compositor_offscreen(compositor);
	}
}


/**
 * Handle KMS GPU being added/removed
 *
 * If the device being added/removed is the KMS device, we activate/deactivate
 * the compositor session.
 *
 * @param compositor The compositor instance.
 * @param device The device being added/removed.
 * @param added Whether the device is being added (or removed)
 */
static void
drm_device_changed(struct weston_compositor *compositor,
		dev_t device, bool added)
{
	struct drm_backend *b = to_drm_backend(compositor);

	if (b->drm.fd < 0 || b->drm.devnum != device ||
	    compositor->session_active == added)
		return;

	compositor->session_active = added;
	wl_signal_emit(&compositor->session_signal, compositor);
}

/**
 * Determines whether or not a device is capable of modesetting. If successful,
 * sets b->drm.fd and b->drm.filename to the opened device.
 */
static bool
drm_device_is_kms(struct drm_backend *b, struct udev_device *device)
{
	const char *filename = udev_device_get_devnode(device);
	const char *sysnum = udev_device_get_sysnum(device);
	dev_t devnum = udev_device_get_devnum(device);
	drmModeRes *res;
	int id = -1, fd;

	if (!filename)
		return false;

	fd = weston_launcher_open(b->compositor->launcher, filename, O_RDWR);
	if (fd < 0)
		return false;

	res = drmModeGetResources(fd);
	if (!res)
		goto out_fd;
	if (res->count_crtcs <= 0 || res->count_connectors <= 0 ||
	    res->count_encoders <= 0)
		goto out_res;

	if (sysnum)
		id = atoi(sysnum);
	if (!sysnum || id < 0) {
		weston_log("couldn't get sysnum for device %s\n", filename);
		goto out_res;
	}

	/* We can be called successfully on multiple devices; if we have,
	 * clean up old entries. */
	if (b->drm.fd >= 0)
		weston_launcher_close(b->compositor->launcher, b->drm.fd);
	free(b->drm.filename);

	b->drm.fd = fd;
	b->drm.id = id;
	b->drm.filename = strdup(filename);
	b->drm.devnum = devnum;

	drmModeFreeResources(res);

	return true;

out_res:
	drmModeFreeResources(res);
out_fd:
	weston_launcher_close(b->compositor->launcher, fd);
	return false;
}

/*
 * Find primary GPU
 * Some systems may have multiple DRM devices attached to a single seat. This
 * function loops over all devices and tries to find a PCI device with the
 * boot_vga sysfs attribute set to 1.
 * If no such device is found, the first DRM device reported by udev is used.
 * Devices are also vetted to make sure they are are capable of modesetting,
 * rather than pure render nodes (GPU with no display), or pure
 * memory-allocation devices (VGEM).
 */
static struct udev_device*
find_primary_gpu(struct drm_backend *b, const char *seat)
{
	struct udev_enumerate *e;
	struct udev_list_entry *entry;
	const char *path, *device_seat, *id;
	struct udev_device *device, *drm_device, *pci;

	e = udev_enumerate_new(b->udev);
	udev_enumerate_add_match_subsystem(e, "drm");
	udev_enumerate_add_match_sysname(e, "card[0-9]*");

	udev_enumerate_scan_devices(e);
	drm_device = NULL;
	udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(e)) {
		bool is_boot_vga = false;

		path = udev_list_entry_get_name(entry);
		device = udev_device_new_from_syspath(b->udev, path);
		if (!device)
			continue;
		device_seat = udev_device_get_property_value(device, "ID_SEAT");
		if (!device_seat)
			device_seat = default_seat;
		if (strcmp(device_seat, seat)) {
			udev_device_unref(device);
			continue;
		}

		pci = udev_device_get_parent_with_subsystem_devtype(device,
								"pci", NULL);
		if (pci) {
			id = udev_device_get_sysattr_value(pci, "boot_vga");
			if (id && !strcmp(id, "1"))
				is_boot_vga = true;
		}

		/* If we already have a modesetting-capable device, and this
		 * device isn't our boot-VGA device, we aren't going to use
		 * it. */
		if (!is_boot_vga && drm_device) {
			udev_device_unref(device);
			continue;
		}

		/* Make sure this device is actually capable of modesetting;
		 * if this call succeeds, b->drm.{fd,filename} will be set,
		 * and any old values freed. */
		if (!drm_device_is_kms(b, device)) {
			udev_device_unref(device);
			continue;
		}

		/* There can only be one boot_vga device, and we try to use it
		 * at all costs. */
		if (is_boot_vga) {
			if (drm_device)
				udev_device_unref(drm_device);
			drm_device = device;
			break;
		}

		/* Per the (!is_boot_vga && drm_device) test above, we only
		 * trump existing saved devices with boot-VGA devices, so if
		 * we end up here, this must be the first device we've seen. */
		assert(!drm_device);
		drm_device = device;
	}

	/* If we're returning a device to use, we must have an open FD for
	 * it. */
	assert(!!drm_device == (b->drm.fd >= 0));

	udev_enumerate_unref(e);
	return drm_device;
}

static struct udev_device *
open_specific_drm_device(struct drm_backend *b, const char *name)
{
	struct udev_device *device;

	device = udev_device_new_from_subsystem_sysname(b->udev, "drm", name);
	if (!device) {
		weston_log("ERROR: could not open DRM device '%s'\n", name);
		return NULL;
	}

	if (!drm_device_is_kms(b, device)) {
		udev_device_unref(device);
		weston_log("ERROR: DRM device '%s' is not a KMS device.\n", name);
		return NULL;
	}

	/* If we're returning a device to use, we must have an open FD for
	 * it. */
	assert(b->drm.fd >= 0);

	return device;
}

static void
planes_binding(struct weston_keyboard *keyboard, const struct timespec *time,
	       uint32_t key, void *data)
{
	struct drm_backend *b = data;

	switch (key) {
	case KEY_C:
		b->cursors_are_broken ^= true;
		break;
	case KEY_V:
		/* We don't support overlay-plane usage with legacy KMS. */
		if (b->atomic_modeset)
			b->sprites_are_broken ^= true;
		break;
	default:
		break;
	}
}

static const struct weston_drm_output_api api = {
	drm_output_set_mode,
	drm_output_set_gbm_format,
	drm_output_set_seat,
};

static struct drm_backend *
drm_backend_create(struct weston_compositor *compositor,
		   struct weston_drm_backend_config *config)
{
	struct drm_backend *b;
	struct udev_device *drm_device;
	struct wl_event_loop *loop;
	const char *seat_id = default_seat;
	const char *session_seat;
	sdm_cbs_t sdm_cbs;
	int ret;
	bool is_gpu_available = true;

	session_seat = getenv("XDG_SEAT");
	if (session_seat)
		seat_id = session_seat;

	if (config->seat_id)
		seat_id = config->seat_id;

	weston_log("initializing SDM backend\n");

	b = zalloc(sizeof *b);
	if (b == NULL)
		return NULL;

	b->state_invalid = true;
	b->drm.fd = -1;
	wl_array_init(&b->unused_crtcs);

	b->compositor = compositor;
	b->use_pixman = config->use_pixman;
	b->pageflip_timeout = config->pageflip_timeout;
	b->use_pixman_shadow = config->use_pixman_shadow;

	b->debug = weston_compositor_add_log_scope(compositor->weston_log_ctx,
						   "drm-backend",
						   "Debug messages from DRM/KMS backend\n",
						    NULL, NULL, NULL);

	compositor->backend = &b->base;
	int fd = open("/dev/kgsl-3d0", O_RDWR);
	if (fd < 0) {
		is_gpu_available = false;
	}
	if (fd >= 0) {
		close(fd);
		fd = -1;
	}
	if (!is_gpu_available) {
		if (parse_gbm_format(config->gbm_format, GBM_FORMAT_ARGB8888, &b->gbm_format) < 0)
		/* Mesa GBM doesn't support GBM_FORMAT_ABGR8888, passing format supported by mesa.*/
			goto err_compositor;
	} else {
		if (parse_gbm_format(config->gbm_format, GBM_FORMAT_ABGR8888, &b->gbm_format) < 0)
			goto err_compositor;
	}

	if (b->use_pixman || !is_gpu_available) {
		b->use_pixman = true;
		weston_log("Use Pixman Rendering - SDM backend\n");
	}

	/* Check if we run drm-backend using weston-launch */
	compositor->launcher = weston_launcher_connect(compositor, config->tty,
						       seat_id, true);
	if (compositor->launcher == NULL) {
		weston_log("fatal: drm backend should be run using "
			   "weston-launch binary, or your system should "
			   "provide the logind D-Bus API.\n");
		goto err_compositor;
	}

	b->udev = udev_new();
	if (b->udev == NULL) {
		weston_log("failed to initialize udev context\n");
		goto err_launcher;
	}

	b->session_listener.notify = session_notify;
	wl_signal_add(&compositor->session_signal, &b->session_listener);

	if (config->specific_device)
		drm_device = open_specific_drm_device(b, config->specific_device);
	else
		drm_device = find_primary_gpu(b, seat_id);

	if (drm_device == NULL) {
		weston_log("no drm device found\n");
		goto err_udev;
	}

	if (init_kms_caps(b) < 0)
		goto err_udev_dev;

	if (b->use_pixman) {
		if (init_pixman(b) < 0) {
			weston_log("failed to initialize pixman renderer\n");
			goto err_udev_dev;
		}
	} else {
		if (init_egl(b) < 0) {
			weston_log("failed to initialize egl\n");
			goto err_udev_dev;
		}
	}

	b->base.destroy = drm_destroy;
	b->base.repaint_begin = drm_repaint_begin;
	b->base.repaint_flush = drm_repaint_flush;
	b->base.repaint_cancel = drm_repaint_cancel;
	b->base.create_output = drm_output_create;
	b->base.device_changed = drm_device_changed;
	b->base.can_scanout_dmabuf = drm_can_scanout_dmabuf;

	weston_setup_vt_switch_bindings(compositor);

	if (udev_input_init(&b->input,
			    compositor, b->udev, seat_id,
			    config->configure_device) < 0) {
		weston_log("failed to create input devices\n");
		goto err_sprite;
	}

	/* A this point we have some idea of whether or not we have a working
	 * cursor plane. */
	if (!b->cursors_are_broken)
		compositor->capabilities |= WESTON_CAP_CURSOR_PLANE;

	loop = wl_display_get_event_loop(compositor->wl_display);

	b->udev_monitor = udev_monitor_new_from_netlink(b->udev, "udev");
	if (b->udev_monitor == NULL) {
		weston_log("failed to initialize udev monitor\n");
		goto err_drm_source;
	}
	udev_monitor_filter_add_match_subsystem_devtype(b->udev_monitor,
							"drm", NULL);
	b->udev_drm_source =
		wl_event_loop_add_fd(loop,
				     udev_monitor_get_fd(b->udev_monitor),
				     WL_EVENT_READABLE, udev_drm_event, b);
	if (b->udev_drm_source == NULL) {
		weston_log("failed to add wl event loop\n");
		udev_monitor_unref(b->udev_monitor);
		goto err_drm_source;
	}

	if (udev_monitor_enable_receiving(b->udev_monitor) < 0) {
		weston_log("failed to enable udev-monitor receiving\n");
		goto err_udev_monitor;
	}

	udev_device_unref(drm_device);

	weston_compositor_add_debug_binding(compositor, KEY_O,
					    planes_binding, b);
	weston_compositor_add_debug_binding(compositor, KEY_C,
					    planes_binding, b);
	weston_compositor_add_debug_binding(compositor, KEY_V,
					    planes_binding, b);
	weston_compositor_add_debug_binding(compositor, KEY_W,
					    renderer_switch_binding, b);

	/* begin SDM initialization */
	int rc = CreateCore(b->use_pixman);
	if (rc != 0) {
		weston_log("Creating SDM core failed");
		return rc;
	}

	if (drm_backend_create_heads(b, drm_device) < 0) {
		weston_log("Failed to create heads for %s\n", b->drm.filename);
		goto err_udev_input;
	}

	if (compositor->renderer->import_dmabuf) {
		if (linux_dmabuf_setup(compositor) < 0)
			weston_log("Error: initializing dmabuf "
				   "support failed.\n");
		if (weston_direct_display_setup(compositor) < 0)
			weston_log("Error: initializing direct-display "
				   "support failed.\n");
	}

	if (compositor->renderer->import_gbm_buffer) {
		if (gbm_buffer_backend_setup(compositor) < 0)
			weston_log("Error: gbm buffer backend setup failed\n");

	}

	if (weston_qti_extn_setup(compositor) < 0)
		weston_log("Error: weston_qti_extn_setup failed\n");

	if (compositor->capabilities & WESTON_CAP_EXPLICIT_SYNC) {
		if (linux_explicit_synchronization_setup(compositor) < 0)
			weston_log("Error: initializing explicit "
				   " synchronization support failed.\n");
	}

	if (b->atomic_modeset)
		if (weston_compositor_enable_content_protection(compositor) < 0)
			weston_log("Error: initializing content-protection "
				   "support failed.\n");

	ret = weston_plugin_api_register(compositor, WESTON_DRM_OUTPUT_API_NAME,
					 &api, sizeof(api));

	if (ret < 0) {
		weston_log("Failed to register output API.\n");
		goto err_udev_monitor;
	}

	return b;

err_udev_monitor:
	wl_event_source_remove(b->udev_drm_source);
	udev_monitor_unref(b->udev_monitor);
err_drm_source:
	wl_event_source_remove(b->drm_source);
err_udev_input:
	udev_input_destroy(&b->input);
err_sprite:
#ifdef BUILD_DRM_GBM
	if (b->gbm)
		gbm_device_destroy(b->gbm);
#endif
err_udev_dev:
	udev_device_unref(drm_device);
err_launcher:
	weston_launcher_destroy(compositor->launcher);
err_udev:
	udev_unref(b->udev);
err_compositor:
	weston_compositor_shutdown(compositor);
	free(b);
	return NULL;
}

static void
config_init_to_defaults(struct weston_drm_backend_config *config)
{
	config->use_pixman_shadow = true;
}

WL_EXPORT int
weston_backend_init(struct weston_compositor *compositor,
		    struct weston_backend_config *config_base)
{
	struct drm_backend *b;
	struct weston_drm_backend_config config = {{ 0, }};

	if (config_base == NULL ||
	    config_base->struct_version != WESTON_DRM_BACKEND_CONFIG_VERSION ||
	    config_base->struct_size > sizeof(struct weston_drm_backend_config)) {
		weston_log("drm backend config structure is invalid\n");
		return -1;
	}

	config_init_to_defaults(&config);
	memcpy(&config, config_base, config_base->struct_size);

	b = drm_backend_create(compositor, &config);
	if (b == NULL)
		return -1;

	return 0;
}

void NotifyOnRefresh(struct drm_output *drm_output) {
  drm_output->base.repaint_status = REPAINT_AWAITING_COMPLETION;
  drm_output->atomic_complete_pending = true;
}
