/*
 * Copyright © 2008-2011 Kristian Høgsberg
 * Copyright © 2011 Intel Corporation
 * Copyright © 2012 Raspberry Pi Foundation
 * Copyright © 2013 Philip Withnall
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
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <sys/eventfd.h>
#ifdef USE_SDM
#include <stdint.h>
#include <linux/msm_mdp.h>
#include "sdm_display_connect.h"
#endif
#include <libudev.h>
#ifdef USE_GBM
#include <gbm.h>
#include <gbm_priv.h>
#endif

#include "shared/timespec-util.h"
#include "shared/helpers.h"
#include "compositor.h"
#include "compositor-fbdev.h"
#include "launcher-util.h"
#include "pixman-renderer.h"
#include "libinput-seat.h"
#include "presentation-time-server-protocol.h"
#include "gl-renderer.h"


struct fbdev_backend {
	struct weston_backend base;
	struct weston_compositor *compositor;
	uint32_t prev_state;

	struct udev *udev;
	struct udev_input input;
	uint32_t output_transform;
	struct wl_listener session_listener;
	bool use_pixman;
	void *buffer_alloc_dev;
	struct udev_monitor *udev_monitor;
	struct wl_event_source *udev_fb_source;
	struct fbdev_output *output;
	bool secondary_connected;
};

struct fbdev_screeninfo {
	unsigned int x_resolution; /* pixels, visible area */
	unsigned int y_resolution; /* pixels, visible area */
	unsigned int width_mm; /* visible screen width in mm */
	unsigned int height_mm; /* visible screen height in mm */
	unsigned int bits_per_pixel;

	size_t buffer_length; /* length of frame buffer memory in bytes */
	size_t line_length; /* length of a line in bytes */
	char id[16]; /* screen identifier */

	pixman_format_code_t pixel_format; /* frame buffer pixel format */
	unsigned int refresh_rate; /* Hertz */
};

struct buffer_allocator {
#ifdef USE_GBM
	struct gbm_surface *surface;
	struct gbm_bo *last_bo;
	struct gbm_bo *current_bo;
#endif
};

struct fbdev_output {
	struct fbdev_backend *backend;
	struct weston_output base;

	struct weston_mode mode;

	/* Frame buffer details. */
	char *device;
	struct fbdev_screeninfo fb_info;
	void *fb; /* length is fb_info.buffer_length */

	/* pixman details. */
	pixman_image_t *hw_surface;
	uint8_t depth;
	struct buffer_allocator buf_alloc;
	int fb_device_fd;

	/* vsync details. */
	struct wl_event_source *vsync_ev_source;
	bool frame_pending;
};

static int vsync_ev_fd = -1;
static int64_t last_vsync_ns = -1;

static struct gl_renderer_interface *gl_renderer;
static const char default_seat[] = "seat0";
static int surface_acquire_buffer(struct fbdev_output *output);
static void surface_release_buffer(struct fbdev_output *output);
static void surface_create(struct fbdev_output *output, struct fbdev_backend *backend);
static void create_buff_alloc_device(int fb_fd, struct fbdev_backend * backend);
static void fbdev_output_fini_egl(struct fbdev_output *output);
static void fbdev_set_dpms(struct weston_output *output_base, enum dpms_enum level);
static void fbdev_set_backlight(struct weston_output *output_base, uint32_t value);
static int fbdev_get_backlight();
static void fbdev_output_flush(struct weston_output *base);
static int fbdev_output_update(struct weston_output *base, const char *device);
static int udev_fb_event(int fd, uint32_t mask, void *data);
static int udev_event_is_hotplug(struct fbdev_backend *backend, struct udev_device *dev);
static int ion_open();
static bool ReadHDMISysfs();

#ifdef USE_SDM
int display_id = -1;
#endif
static void buffer_destroy(struct buffer_allocator buf_alloc);

static void
vsync_handler(int64_t timestamp)
{
	uint64_t v = 1;

	last_vsync_ns = timestamp;
	/* Revisit: Event is queued to pollfd list,
	 * Can cause inconsistent delays for higher FPS.
	 */
	write(vsync_ev_fd, &v, sizeof v);
}

static int
on_vsync(int fd, uint32_t mask, void *data)
{
	struct fbdev_output *output = (struct fbdev_output *) data;
	uint64_t v;
	int ret = 0;
	int ion_fd;
	struct weston_compositor *ec = output->base.compositor;

	read(fd, &v, sizeof v);

	output_repaint_timer_handler(ec);
	if (output->frame_pending) {
		output->frame_pending = false;
#ifdef USE_SDM
		ion_fd = output->buf_alloc.current_bo->ion_fd;
		ret = Commit(display_id, ion_fd);
		if (ret) {
			weston_log("fail to commit to sdm display! err=%d\n", ret);
			output->frame_pending = true;
			return 0;
		}

#else
		fbdev_output_display(output);
#endif
		if(!output->backend->use_pixman) {
			surface_release_buffer(output);
		}
	}

	return 1;
}

static inline struct fbdev_output *
to_fbdev_output(struct weston_output *base)
{
	return container_of(base, struct fbdev_output, base);
}

static inline struct fbdev_backend *
to_fbdev_backend(struct weston_compositor *base)
{
	return container_of(base->backend, struct fbdev_backend, base);
}

static void
fbdev_output_start_repaint_loop(struct weston_output *output)
{
	struct timespec ts;

	weston_compositor_read_presentation_clock(output->compositor, &ts);
	weston_output_finish_frame(output, &ts, WP_PRESENTATION_FEEDBACK_INVALID);
}

/* Call FBIOPUT_VSCREENINFO ioctl as FB driver will refresh the screen
   when FBIOPUT_VSCREENINFO is called */
static void
fbdev_output_display(struct fbdev_output *output)
{
	struct fb_var_screeninfo varinfo;
	if (ioctl(output->fb_device_fd, FBIOGET_VSCREENINFO, &varinfo) < 0) {
		weston_log("FBIOGET_VSCREENINFO failure \n ");
	}
	varinfo.grayscale=0;
	varinfo.yres_virtual = output->fb_info.y_resolution;
	varinfo.yoffset = 0;
	varinfo.bits_per_pixel = 32;
	if (ioctl(output->fb_device_fd,  FBIOPUT_VSCREENINFO, &varinfo) < 0) {
		weston_log("FBIOPUT_VSCREENINFO failure \n ");
	}
}

#ifdef USE_SDM
static int
get_output_fd(struct fbdev_output *output)
{
	struct msmfb_metadata metadata;
	int fd = open(output->device, O_RDWR | O_CLOEXEC);
	if (fd > 0) {
		weston_log("%s(%d): FB opened \n",__func__,__LINE__);
    }
	memset(&metadata, 0 , sizeof(metadata));
	metadata.op = metadata_op_get_ion_fd;
	if (ioctl(fd, MSMFB_METADATA_GET, &metadata) == -1) {
		weston_log("%s(%d): MSMFB_METADATA_GET ioctl failed \n",__func__,__LINE__);
		return -1;
	}
	if(metadata.data.fbmem_ionfd < 0) {
		weston_log("%s(%d): Invalid ion fd handle %d\n",__func__,__LINE__, metadata.data.fbmem_ionfd);
		return -1;
  }
	close(fd);
	return metadata.data.fbmem_ionfd;
}
#endif

static int
fbdev_output_repaint(struct weston_output *base, pixman_region32_t *damage,
		     void *repaint_data)
{
	struct fbdev_output *output = to_fbdev_output(base);
	struct weston_compositor *ec = output->base.compositor;
	struct fbdev_backend *fbb = output->backend;

	if (fbb->use_pixman) {
		/* Repaint the damaged region onto the back buffer. */
		pixman_renderer_output_set_buffer(base, output->hw_surface);
		ec->renderer->repaint_output(base, damage);

	} else {
		ec->renderer->repaint_output(base, damage);
		if (surface_acquire_buffer(output) < 0) {
			weston_log("Acquire Buffer Failed. Repaint Unsuccessful!\n");
			return -1;
		}
	}
	/* Update the damage region. */
		pixman_region32_subtract(&ec->primary_plane.damage,
	                         &ec->primary_plane.damage, damage);

	/* Schedule the end of the frame. We do not sync this to the frame
	 * buffer clock because users who want that should be using the DRM
	 * compositor. FBIO_WAITFORVSYNC blocks and FB_ACTIVATE_VBL requires
	 * panning, which is broken in most kernel drivers.
	 *
	 * Finish the frame synchronised to the specified refresh rate. The
	 * refresh rate is given in mHz and the interval in ms. */

	output->frame_pending = true;

	return 0;
}

static pixman_format_code_t
calculate_pixman_format(struct fb_var_screeninfo *vinfo,
                        struct fb_fix_screeninfo *finfo)
{
	/* Calculate the pixman format supported by the frame buffer from the
	 * buffer's metadata. Return 0 if no known pixman format is supported
	 * (since this has depth 0 it's guaranteed to not conflict with any
	 * actual pixman format).
	 *
	 * Documentation on the vinfo and finfo structures:
	 *    http://www.mjmwired.net/kernel/Documentation/fb/api.txt
	 *
	 * TODO: Try a bit harder to support other formats, including setting
	 * the preferred format in the hardware. */
	int type;

	weston_log("Calculating pixman format from:\n"
	           STAMP_SPACE " - type: %i (aux: %i)\n"
	           STAMP_SPACE " - visual: %i\n"
	           STAMP_SPACE " - bpp: %i (grayscale: %i)\n"
	           STAMP_SPACE " - red: offset: %i, length: %i, MSB: %i\n"
	           STAMP_SPACE " - green: offset: %i, length: %i, MSB: %i\n"
	           STAMP_SPACE " - blue: offset: %i, length: %i, MSB: %i\n"
	           STAMP_SPACE " - transp: offset: %i, length: %i, MSB: %i\n",
	           finfo->type, finfo->type_aux, finfo->visual,
	           vinfo->bits_per_pixel, vinfo->grayscale,
	           vinfo->red.offset, vinfo->red.length, vinfo->red.msb_right,
	           vinfo->green.offset, vinfo->green.length,
	           vinfo->green.msb_right,
	           vinfo->blue.offset, vinfo->blue.length,
	           vinfo->blue.msb_right,
	           vinfo->transp.offset, vinfo->transp.length,
	           vinfo->transp.msb_right);

	/* We only handle packed formats at the moment. */
	if (finfo->type != FB_TYPE_PACKED_PIXELS)
		return 0;

	/* We only support formats with MSBs on the left. */
	if (vinfo->red.msb_right != 0 || vinfo->green.msb_right != 0 ||
	    vinfo->blue.msb_right != 0)
		return 0;

	/* Work out the format type from the offsets. We only support RGBA and
	 * ARGB at the moment. */
	type = PIXMAN_TYPE_ARGB;

	if ((vinfo->transp.offset >= vinfo->red.offset ||
	     vinfo->transp.length == 0) &&
	    vinfo->red.offset >= vinfo->green.offset &&
	    vinfo->green.offset >= vinfo->blue.offset)
		type = PIXMAN_TYPE_ARGB;
	else if (vinfo->red.offset >= vinfo->green.offset &&
	         vinfo->green.offset >= vinfo->blue.offset &&
	         vinfo->blue.offset >= vinfo->transp.offset)
		type = PIXMAN_TYPE_RGBA;

	if (type == PIXMAN_TYPE_OTHER)
		return 0;

	/* Build the format. */
	return PIXMAN_FORMAT(vinfo->bits_per_pixel, type,
	                     vinfo->transp.length,
	                     vinfo->red.length,
	                     vinfo->green.length,
	                     vinfo->blue.length);
}

static int
calculate_refresh_rate(struct fb_var_screeninfo *vinfo)
{
	uint64_t quot;

	/* Calculate monitor refresh rate. Default is 60 Hz. Units are mHz. */
	quot = (vinfo->upper_margin + vinfo->lower_margin + vinfo->yres);
	quot *= (vinfo->left_margin + vinfo->right_margin + vinfo->xres);
	quot *= vinfo->pixclock;

	if (quot > 0) {
		uint64_t refresh_rate;

		refresh_rate = 1000000000000000LLU / quot;
		if (refresh_rate > 200000)
			refresh_rate = 200000; /* cap at 200 Hz */

		if (refresh_rate >= 1000) /* at least 1 Hz */
			return refresh_rate;
	}

	return 60 * 1000; /* default to 60 Hz */
}

static int
fbdev_query_screen_info(struct fbdev_output *output, int fd,
                        struct fbdev_screeninfo *info)
{
	struct fb_var_screeninfo varinfo;
	struct fb_fix_screeninfo fixinfo;

	/* Probe the device for screen information. */
	if (ioctl(fd, FBIOGET_FSCREENINFO, &fixinfo) < 0 ||
	    ioctl(fd, FBIOGET_VSCREENINFO, &varinfo) < 0) {
		return -1;
	}

	/* Store the pertinent data. */
	info->x_resolution = varinfo.xres;
	info->y_resolution = varinfo.yres;
	info->width_mm = varinfo.width;
	info->height_mm = varinfo.height;
	info->bits_per_pixel = varinfo.bits_per_pixel;

	info->buffer_length = fixinfo.smem_len;
	info->line_length = fixinfo.line_length;
	strncpy(info->id, fixinfo.id, sizeof(info->id));
	info->id[sizeof(info->id)-1] = '\0';

	info->pixel_format = calculate_pixman_format(&varinfo, &fixinfo);
	info->refresh_rate = calculate_refresh_rate(&varinfo);

	if (info->pixel_format == 0) {
		weston_log("Frame buffer uses an unsupported format.\n");
		return -1;
	}

	return 1;
}

static int
fbdev_set_screen_info(struct fbdev_output *output, int fd,
                      struct fbdev_screeninfo *info)
{
	struct fb_var_screeninfo varinfo;

	/* Grab the current screen information. */
	if (ioctl(fd, FBIOGET_VSCREENINFO, &varinfo) < 0) {
		return -1;
	}

	/* Update the information. */
	varinfo.xres = info->x_resolution;
	varinfo.yres = info->y_resolution;
	varinfo.width = info->width_mm;
	varinfo.height = info->height_mm;
	varinfo.bits_per_pixel = info->bits_per_pixel;

	/* Try to set up an ARGB (x8r8g8b8) pixel format. */
	varinfo.grayscale = 0;
	varinfo.transp.offset = 24;
	varinfo.transp.length = 0;
	varinfo.transp.msb_right = 0;
	varinfo.red.offset = 16;
	varinfo.red.length = 8;
	varinfo.red.msb_right = 0;
	varinfo.green.offset = 8;
	varinfo.green.length = 8;
	varinfo.green.msb_right = 0;
	varinfo.blue.offset = 0;
	varinfo.blue.length = 8;
	varinfo.blue.msb_right = 0;

	/* Set the device's screen information. */
	if (ioctl(fd, FBIOPUT_VSCREENINFO, &varinfo) < 0) {
		return -1;
	}

	return 1;
}

static void fbdev_frame_buffer_destroy(struct fbdev_output *output);

/* Returns an FD for the frame buffer device. */
static int
fbdev_frame_buffer_open(struct fbdev_output *output, const char *fb_dev,
                        struct fbdev_screeninfo *screen_info)
{
	int fd = -1;

	weston_log("Opening fbdev frame buffer.\n");

	/* Open the frame buffer device. */
	fd = open(fb_dev, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		weston_log("Failed to open frame buffer device '%s': %s\n",
		           fb_dev, strerror(errno));
		return -1;
	}

	/* Grab the screen info. */
	if (fbdev_query_screen_info(output, fd, screen_info) < 0) {
		weston_log("Failed to get frame buffer info: %s\n",
		           strerror(errno));

		close(fd);
		return -1;
	}

	return fd;
}

/* Closes the FD on success or failure. */
static int
fbdev_frame_buffer_map(struct fbdev_output *output, int fd)
{
	int retval = -1;

	weston_log("Mapping fbdev frame buffer.\n");

	/* Map the frame buffer. Write-only mode, since we don't want to read
	 * anything back (because it's slow). */
	output->fb = mmap(NULL, output->fb_info.buffer_length,
	                  PROT_WRITE, MAP_SHARED, fd, 0);
	if (output->fb == MAP_FAILED) {
		weston_log("Failed to mmap frame buffer: %s\n",
		           strerror(errno));
		goto out_close;
	}

	/* Create a pixman image to wrap the memory mapped frame buffer. */
	output->hw_surface =
		pixman_image_create_bits(output->fb_info.pixel_format,
		                         output->fb_info.x_resolution,
		                         output->fb_info.y_resolution,
		                         output->fb,
		                         output->fb_info.line_length);
	if (output->hw_surface == NULL) {
		weston_log("Failed to create surface for frame buffer.\n");
		goto out_unmap;
	}

	/* Success! */
	retval = 0;

out_unmap:
	if (retval != 0 && output->fb != NULL)
		fbdev_frame_buffer_destroy(output);

out_close:
	if (fd >= 0)
		close(fd);

	return retval;
}

static void
fbdev_frame_buffer_destroy(struct fbdev_output *output)
{
	weston_log("Destroying fbdev frame buffer.\n");

	if (munmap(output->fb, output->fb_info.buffer_length) < 0)
		weston_log("Failed to munmap frame buffer: %s\n",
		           strerror(errno));

	output->fb = NULL;
}

static int
fbdev_output_enable_vsync(struct fbdev_output *output)
{
     struct wl_event_loop *loop;

     vsync_ev_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
     if (vsync_ev_fd < 0)
        return -1;

     loop = wl_display_get_event_loop(output->base.compositor->wl_display);
     output->vsync_ev_source = wl_event_loop_add_fd(loop, vsync_ev_fd,
                                                     WL_EVENT_READABLE, on_vsync, output);

     return 0;
}

static void
fbdev_output_disable_vsync(struct fbdev_output *output)
{
       if (output->vsync_ev_source != NULL) {
               wl_event_source_remove(output->vsync_ev_source);
               output->vsync_ev_source = NULL;
       }

       if (vsync_ev_fd != -1) {
               close(vsync_ev_fd);
               vsync_ev_fd = -1;
       }
}

static void fbdev_output_destroy(struct weston_output *base);
static void fbdev_output_disable(struct weston_output *base);

/* Init output state that depends on gl or gbm */
static int
fbdev_output_init_egl(struct fbdev_output *output, struct fbdev_backend *b)
{
	uint32_t gbm_format = (output->fb_info.bits_per_pixel == BPP_16) ?
						GBM_FORMAT_RGB565 : GBM_FORMAT_ABGR8888;
	EGLint format[1] = {
		gbm_format,
		GBM_FORMAT_ARGB8888,
	};
	int n_formats = 2;
	surface_create(output, b);
	void *surface = NULL;
	surface = output->buf_alloc.surface;

	if (!surface) {
		weston_log("failed to create gbm surface\n");
		return -1;
	}

	if (gl_renderer->output_window_create(&output->base,
					      (EGLNativeWindowType)surface,
					      surface,
					      gl_renderer->opaque_attribs,
					      format,
					      n_formats) < 0) {
		weston_log("failed to create gl renderer output state\n");
		gbm_surface_destroy(surface);
		return -1;
	}

	return 0;
}
static int
fbdev_output_enable(struct weston_output *base)
{
	struct fbdev_output *output = to_fbdev_output(base);
	struct fbdev_backend *backend = to_fbdev_backend(base->compositor);
	int fb_fd;
	struct wl_event_loop *loop;

	/* Create the frame buffer. */
	fb_fd = fbdev_frame_buffer_open(output, output->device, &output->fb_info);
	if (fb_fd < 0) {
		weston_log("Creating frame buffer failed.\n");
		return -1;
	}

	if (fbdev_frame_buffer_map(output, fb_fd) < 0) {
		weston_log("Mapping frame buffer failed.\n");
		return -1;
	}

	output->base.start_repaint_loop = fbdev_output_start_repaint_loop;
	output->base.repaint = fbdev_output_repaint;

	if (backend->use_pixman) {
		if (pixman_renderer_output_create(&output->base) < 0) {
			weston_log("Failed to init output pixman state\n");
			goto out_hw_surface;
		}
	} else if (fbdev_output_init_egl(output, backend) < 0) {
		weston_log("Failed to init output gl state\n");
		goto out_hw_surface;
	}

	if (fbdev_output_enable_vsync(output)) {
		weston_log("Failed to create vsync event\n");
		goto out_hw_surface;
	}

	loop = wl_display_get_event_loop(backend->compositor->wl_display);

	weston_log("fbdev output %dx%d px\n",
	           output->mode.width, output->mode.height);
	weston_log_continue(STAMP_SPACE "guessing %d Hz and 96 dpi\n",
	                    output->mode.refresh / 1000);

	output->fb_device_fd = fb_fd;
	return 0;

out_hw_surface:
	pixman_image_unref(output->hw_surface);
	output->hw_surface = NULL;
	fbdev_frame_buffer_destroy(output);

	return -1;
}

static int
fbdev_output_create(struct fbdev_backend *backend,
                    const char *device)
{
	struct fbdev_output *output;
	int fb_fd;

	weston_log("Creating fbdev output.\n");

	output = zalloc(sizeof *output);
	if (output == NULL)
		return -1;

	output->backend = backend;
	output->device = strdup(device);

	/* Create the frame buffer. */
	fb_fd = fbdev_frame_buffer_open(output, device, &output->fb_info);
	if (fb_fd < 0) {
		weston_log("Creating frame buffer failed.\n");
		goto out_free;
	}

	output->base.name = strdup("fbdev");
	output->base.destroy = fbdev_output_destroy;
	output->base.disable = fbdev_output_disable;
	output->base.enable = fbdev_output_enable;
	output->base.set_dpms = fbdev_set_dpms;
	output->base.set_backlight = fbdev_set_backlight;
	output->base.backlight_current = fbdev_get_backlight();

	weston_output_init(&output->base, backend->compositor);

	/* only one static mode in list */
	output->mode.flags =
		WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED;
	output->mode.width = output->fb_info.x_resolution;
	output->mode.height = output->fb_info.y_resolution;
	output->mode.refresh = output->fb_info.refresh_rate;
	wl_list_init(&output->base.mode_list);
	wl_list_insert(&output->base.mode_list, &output->mode.link);

	output->base.current_mode = &output->mode;
	output->base.subpixel = WL_OUTPUT_SUBPIXEL_UNKNOWN;
	output->base.make = "unknown";
	output->base.model = output->fb_info.id;

	output->base.mm_width = output->fb_info.width_mm;
	output->base.mm_height = output->fb_info.height_mm;
	output->frame_pending = false;


	weston_compositor_add_pending_output(&output->base, backend->compositor);
	backend->output = output;

	close(fb_fd);
	return 0;

out_free:
	free(output->device);
	free(output);

	return -1;
}

static int
fbdev_output_update(struct weston_output *base,
                    const char *device)
{
	struct fbdev_output *output = to_fbdev_output(base);
	int fb_fd;

	weston_log("Updating fbdev output.\n");

	if (output == NULL)
		return -1;

	output->device = strdup(device);

	/* Create the frame buffer. */
	fb_fd = fbdev_frame_buffer_open(output, device, &output->fb_info);
	if (fb_fd < 0) {
		weston_log("Creating frame buffer failed.\n");
		goto out_free;
	}

	output->base.name = strdup("fbdev");

	/* only one static mode in list */
	output->mode.flags =
		WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED;
	output->mode.width = output->fb_info.x_resolution;
	output->mode.height = output->fb_info.y_resolution;
	output->mode.refresh = output->fb_info.refresh_rate;
	wl_list_init(&output->base.mode_list);
	wl_list_insert(&output->base.mode_list, &output->mode.link);

	output->base.current_mode = &output->mode;
	output->base.subpixel = WL_OUTPUT_SUBPIXEL_UNKNOWN;
	output->base.make = "unknown";
	output->base.model = output->fb_info.id;

	output->base.mm_width = output->fb_info.width_mm;
	output->base.mm_height = output->fb_info.height_mm;
	close(fb_fd);
	return 0;

out_free:
	free(output->device);
	free(output);

	return -1;
}

static void
fbdev_output_destroy(struct weston_output *base)
{
	struct fbdev_output *output = to_fbdev_output(base);

	if (output->hw_surface)
		fbdev_output_flush(base);

#ifdef USE_SDM
	SetVSyncState(false);
	DestroyDisplay(display_id);
#endif
	fbdev_output_disable_vsync(output);
	weston_output_destroy(&output->base);
	free(output->device);
	free(output);
}

static void
fbdev_output_flush(struct weston_output *base)
{
	struct fbdev_output *output = to_fbdev_output(base);

	weston_log("Destroying fbdev output.\n");

	/* Close the frame buffer. */
	fbdev_output_disable(base);
	if (base->renderer_state != NULL) {
		if (output->backend->use_pixman) {
			pixman_renderer_output_destroy(base);
		}
		else {
			fbdev_output_fini_egl(output);
		}
	}
	close(output->fb_device_fd);
}

/* strcmp()-style return values. */
static int
compare_screen_info (const struct fbdev_screeninfo *a,
                     const struct fbdev_screeninfo *b)
{
	if (a->x_resolution == b->x_resolution &&
	    a->y_resolution == b->y_resolution &&
	    a->width_mm == b->width_mm &&
	    a->height_mm == b->height_mm &&
	    a->bits_per_pixel == b->bits_per_pixel &&
	    a->pixel_format == b->pixel_format &&
	    a->refresh_rate == b->refresh_rate)
		return 0;

	return 1;
}

static int
fbdev_output_reenable(struct fbdev_backend *backend,
                      struct weston_output *base)
{
	struct fbdev_output *output = to_fbdev_output(base);
	struct fbdev_screeninfo new_screen_info;
	int fb_fd;
	char *device;

	weston_log("Re-enabling fbdev output.\n");

	/* Create the frame buffer. */
	fb_fd = fbdev_frame_buffer_open(output, output->device,
	                                &new_screen_info);
	if (fb_fd < 0) {
		weston_log("Creating frame buffer failed.\n");
		goto err;
	}

	/* Check whether the frame buffer details have changed since we were
	 * disabled. */
	if (compare_screen_info (&output->fb_info, &new_screen_info) != 0) {
		/* Perform a mode-set to restore the old mode. */
		if (fbdev_set_screen_info(output, fb_fd,
		                          &output->fb_info) < 0) {
			weston_log("Failed to restore mode settings. "
			           "Attempting to re-open output anyway.\n");
		}

		close(fb_fd);

		/* Remove and re-add the output so that resources depending on
		 * the frame buffer X/Y resolution (such as the shadow buffer)
		 * are re-initialised. */
		device = strdup(output->device);
		fbdev_output_destroy(&output->base);
		fbdev_output_create(backend, device);
		free(device);

		return 0;
	}

	/* Map the device if it has the same details as before. */
	if (fbdev_frame_buffer_map(output, fb_fd) < 0) {
		weston_log("Mapping frame buffer failed.\n");
		goto err;
	}

	return 0;

err:
	return -1;
}

/* NOTE: This leaves output->fb_info populated, caching data so that if
 * fbdev_output_reenable() is called again, it can determine whether a mode-set
 * is needed. */
static void
fbdev_output_disable(struct weston_output *base)
{
	struct fbdev_output *output = to_fbdev_output(base);

	weston_log("Disabling fbdev output.\n");

	if (output->hw_surface != NULL) {
		pixman_image_unref(output->hw_surface);
		output->hw_surface = NULL;
	}

	fbdev_frame_buffer_destroy(output);
}

static void
fbdev_backend_destroy(struct weston_compositor *base)
{
	struct fbdev_backend *backend = to_fbdev_backend(base);

	udev_input_destroy(&backend->input);

	/* Destroy the output. */
	weston_compositor_shutdown(base);

	/* Chain up. */
	weston_launcher_destroy(base->launcher);
#ifdef USE_SDM
	DestroyDisplay(display_id);
	DestroyCore();
#endif
	free(backend);
}

static void
session_notify(struct wl_listener *listener, void *data)
{
	struct weston_compositor *compositor = data;
	struct fbdev_backend *backend = to_fbdev_backend(compositor);
	struct weston_output *output;

	if (compositor->session_active) {
		weston_log("entering VT\n");
		compositor->state = backend->prev_state;

		wl_list_for_each(output, &compositor->output_list, link) {
			fbdev_output_reenable(backend, output);
		}

		weston_compositor_damage_all(compositor);

		udev_input_enable(&backend->input);
	} else {
		weston_log("leaving VT\n");
		udev_input_disable(&backend->input);

		wl_list_for_each(output, &compositor->output_list, link) {
			fbdev_output_disable(output);
		}

		backend->prev_state = compositor->state;
		weston_compositor_offscreen(compositor);

		/* If we have a repaint scheduled (from the idle handler), make
		 * sure we cancel that so we don't try to pageflip when we're
		 * vt switched away.  The OFFSCREEN state will prevent
		 * further attempts at repainting.  When we switch
		 * back, we schedule a repaint, which will process
		 * pending frame callbacks. */

		wl_list_for_each(output,
				 &compositor->output_list, link) {
			output->repaint_needed = false;
		}
	}
}

static void
fbdev_restore(struct weston_compositor *compositor)
{
	weston_launcher_restore(compositor->launcher);
}

static int
fbdev_backend_create_gl_renderer(struct fbdev_backend *b)
{
	EGLint format[3] = {
		GBM_FORMAT_ABGR8888,
		GBM_FORMAT_ARGB8888,
		0,
	};

	int n_formats = 3;
	EGLenum platform = NO_EGL_PLATFORM;
#ifdef USE_GBM
	platform = EGL_PLATFORM_GBM_KHR;
#endif

	if (gl_renderer->display_create(b->compositor,
					platform,
					b->buffer_alloc_dev,
					NULL,
					gl_renderer->opaque_attribs,
					format,
					n_formats) < 0) {
		return -1;
	}

	return 0;
}

static int
init_egl(struct fbdev_backend *b)
{
	gl_renderer = weston_load_module("gl-renderer.so",
					 "gl_renderer_interface");
	if (!gl_renderer) {
		weston_log("Unable to load gl-renderer \n");
		return;
	}
	int fd = ion_open();
	create_buff_alloc_device(fd, b);

	if (!b->buffer_alloc_dev) {
		weston_log("Buffer allocator is NULL \n");
		return -1;
	}

	if (fbdev_backend_create_gl_renderer(b) < 0) {
		weston_log("Unable to create FB backend \n");
		gbm_device_destroy(b->buffer_alloc_dev);
		return -1;
	}

	return 0;
}

static void
fbdev_output_fini_egl(struct fbdev_output *output)
{
	gl_renderer->output_destroy(&output->base);
	buffer_destroy(output->buf_alloc);
}

static struct fbdev_backend *
fbdev_backend_create(struct weston_compositor *compositor,
                     struct weston_fbdev_backend_config *param)
{
	int rc = -1;
	if (strcmp(param->device, PRIMARY_DISPLAY_NODE)!=0 &&
		strcmp(param->device, SECONDARY_DISPLAY_NODE)!=0) {
		weston_log("Incorrect argument \n");
		return NULL;
	}
	struct fbdev_backend *backend;
	const char *seat_id = default_seat;

	weston_log("initializing fbdev backend\n");

	backend = zalloc(sizeof *backend);
	if (backend == NULL)
		return NULL;

	backend->compositor = compositor;
	if (weston_compositor_set_presentation_clock_software(
							compositor) < 0)
		goto out_compositor;


	backend->use_pixman = param->use_pixman;
	/* Set up the TTY. */
	backend->session_listener.notify = session_notify;
	wl_signal_add(&compositor->session_signal,
		      &backend->session_listener);
	compositor->launcher =
		weston_launcher_connect(compositor, param->tty, "seat0", false);
	if (!compositor->launcher) {
		weston_log("fatal: fbdev backend should be run "
			   "using weston-launch binary or as root\n");
		goto out_udev;
	}

	backend->base.destroy = fbdev_backend_destroy;
	backend->base.restore = fbdev_restore;

	backend->prev_state = WESTON_COMPOSITOR_ACTIVE;

	weston_setup_vt_switch_bindings(compositor);

	backend->udev = udev_new();
	if (backend->udev == NULL) {
		weston_log("Failed to initialize udev context.\n");
		goto out_compositor;
	}

	rc = udev_input_init(&backend->input, compositor, backend->udev,
			seat_id, param->configure_device);
	if (rc != 0) {
		weston_log("udev_input_init: failed %d\n",rc);
		goto out_compositor;
	}

#ifdef USE_SDM
    /* begin SDM initialization */
	rc = CreateCore();
	weston_log("CreateCore : returned  %d \n",rc);
	int ret = GetFirstDisplayType(&display_id);
	weston_log("GetFirstDisplayType: display_id = %d \n", display_id);
	/* TODO : Remove default primary creation.
		HPD event is not received until primary display
		is created. So create primary display always even if
		in command line argument is requested to create
		secondary display. Upon receiving HPD destroy primary and
		then create a secondary display.
	*/
	ret = CreateDisplay(display_id);
	weston_log("CreateDisplay: ret = %d \n", ret);
#endif
	backend->secondary_connected = false;
	if (strcmp(param->device, PRIMARY_DISPLAY_NODE)==0) {
		if (fbdev_output_create(backend, param->device) < 0)
		goto out_launcher;

		if (backend->use_pixman) {
			if (pixman_renderer_init(compositor) < 0) {
				weston_log("failed to initialize pixman renderer\n");
				goto out_launcher;
			}
		} else {
			if (init_egl(backend) < 0) {
				weston_log("failed to initialize egl\n");
				goto out_launcher;
			}
		}
#ifdef USE_SDM
		SetVSyncState(true);
		RegisterVSyncCb(display_id, vsync_handler);
#endif
	} else {
		backend->udev_monitor = udev_monitor_new_from_netlink(backend->udev, "udev");
		if (backend->udev_monitor == NULL) {
			weston_log("failed to initialize udev monitor\n");
			goto out_compositor;
		}
		struct wl_event_loop *loop = wl_display_get_event_loop(compositor->wl_display);
		backend->udev_fb_source = wl_event_loop_add_fd(loop,
					udev_monitor_get_fd(backend->udev_monitor),
					WL_EVENT_READABLE, udev_fb_event, backend);
		if (udev_monitor_enable_receiving(backend->udev_monitor) < 0) {
			weston_log("failed to enable udev-monitor receiving\n");
			goto out_compositor;
		}
	}
	compositor->backend = &backend->base;
	return backend;

out_launcher:
	weston_launcher_destroy(compositor->launcher);

out_udev:
	udev_unref(backend->udev);

out_compositor:
	weston_compositor_shutdown(compositor);
	free(backend);

	return NULL;
}

static void
config_init_to_defaults(struct weston_fbdev_backend_config *config)
{
	/* TODO: Ideally, available frame buffers should be enumerated using
	 * udev, rather than passing a device node in as a parameter. */
	config->tty = 0; /* default to current tty */
	config->device = "/dev/fb0"; /* default frame buffer */
	config->use_pixman = false;
}

WL_EXPORT int
weston_backend_init(struct weston_compositor *compositor,
		    struct weston_backend_config *config_base)
{
	struct fbdev_backend *b;
	struct weston_fbdev_backend_config config = {{ 0, }};

	if (config_base == NULL ||
	    config_base->struct_version != WESTON_FBDEV_BACKEND_CONFIG_VERSION ||
	    config_base->struct_size > sizeof(struct weston_fbdev_backend_config)) {
		weston_log("fbdev backend config structure is invalid\n");
		return -1;
	}

	config_init_to_defaults(&config);
	memcpy(&config, config_base, config_base->struct_size);

	b = fbdev_backend_create(compositor, &config);
	if (b == NULL)
		return -1;
	return 0;
}

static int
surface_acquire_buffer(struct fbdev_output *output)
{
#ifdef USE_GBM
	output->buf_alloc.last_bo = output->buf_alloc.current_bo;
	output->buf_alloc.current_bo = gbm_surface_lock_front_buffer(output->buf_alloc.surface);
	if (!output->buf_alloc.current_bo) {
		weston_log("Failed to acquire front buffer\n");
		return -1;
	}
#endif
	return 0;
}

static void
surface_release_buffer(struct fbdev_output *output)
{
#ifdef USE_GBM
	if (output->buf_alloc.last_bo)
		gbm_surface_release_buffer(output->buf_alloc.surface, output->buf_alloc.last_bo);
#endif
}

static void
surface_create(struct fbdev_output *output, struct fbdev_backend *backend)
{
#ifdef USE_GBM
	uint32_t format = (output->fb_info.bits_per_pixel == BPP_16) ?
						GBM_FORMAT_RGB565 : GBM_FORMAT_ABGR8888;
	output->buf_alloc.surface = gbm_surface_create(backend->buffer_alloc_dev, output->mode.width, output->mode.height,
													format, GBM_BO_USE_SCANOUT |GBM_BO_USE_RENDERING);
	output->buf_alloc.last_bo = NULL;
	output->buf_alloc.current_bo = NULL;
	uint32_t stride = 0;
	gbm_perform(GBM_PERFORM_GET_SURFACE_STRIDE, output->buf_alloc.surface, &stride);
	weston_log("FBT stride = %d \n",stride);
	SetLineLength(stride);
#endif
}

static void
create_buff_alloc_device(int fb_fd, struct fbdev_backend * backend)
{
#ifdef USE_GBM
	struct gbm_device * gbmdev = gbm_create_device(fb_fd);
	backend->buffer_alloc_dev = (void *)gbmdev;
#endif
}

static void
buffer_destroy(struct buffer_allocator buf_alloc)
{
#ifdef USE_GBM
	gbm_surface_destroy(buf_alloc.surface);
#endif
}

static void
fbdev_set_wakelock(enum dpms_enum level)
{
	if (level == WESTON_DPMS_ON) {
		int val = system("echo weston_server > /sys/power/wake_lock");
		if(val < 0) {
			weston_log("fbdev_set_dpms: Unable to acquire wake lock \n");
		} else {
				weston_log("fbdev_set_dpms: acquired wake lock \n");
		}
	} else {
		int val = system("echo weston_server > /sys/power/wake_unlock");
		if(val < 0) {
			weston_log("fbdev_set_dpms: Unable to release wake lock \n");
		} else {
				weston_log("fbdev_set_dpms: released wake lock \n");
		}
	}
}

static void
fbdev_set_dpms(struct weston_output *output_base, enum dpms_enum level)
{
	weston_log("fbdev_set_dpms: Calling SetDisplayState weston dpms level = %d \n",level);
	return;
	fbdev_set_wakelock(level);

	/* Turn OFF HW Vsync before suspend
	   Turn ON HW Vsync after resume
	*/
	int state = kDisplayStateOff;
	if (level == WESTON_DPMS_ON) {
		state = kDisplayStateOn;
	}

	if (state == kDisplayStateOff) {
		SetVSyncState(false);
	}
	int ret = SetDisplayState(state);
	if (ret) {
		weston_log("fbdev_set_dpms: SetDisplayState failed. \n");
		return;
	}
	if(state == kDisplayStateOn) {
		SetVSyncState(true);
	}
}

static void
switch_display(int old_disp_id, int new_disp_id,
			struct weston_output * output,
			struct fbdev_backend *backend, const char * new_node, int is_new)
{
	DestroyDisplay(old_disp_id);
	CreateDisplay(new_disp_id);
	weston_log("switch display (%d)->(%d) is_new(%d)\n", old_disp_id, new_disp_id, is_new);
	display_id = new_disp_id;

	if (is_new) {
		if (init_egl(backend) < 0) {
			weston_log("failed to initialize egl\n");
		}
		weston_log("create output(%s)\n",new_node);
		fbdev_output_create(backend, new_node);
	} else {
		weston_log("update output\n");
		fbdev_output_update(output, new_node);
		fbdev_output_enable(output);
		weston_output_enable(output);
	}

	/*turn on vsync for ext display*/
	SetVSyncState(true);
	RegisterVSyncCb(display_id, vsync_handler);
}

static int
udev_fb_event(int fd, uint32_t mask, void *data)
{
	struct fbdev_backend *b = data;
	struct udev_device *dev;
	static bool first_time = true;
	dev = udev_monitor_receive_device(b->udev_monitor);
	if (udev_event_is_hotplug(b, dev)) {
		bool connected = ReadHDMISysfs();
		if (!first_time && !connected && b->secondary_connected) {
			DestroyDisplay(SECONDARY_DISPLAY_ID);
			CreateDisplay(PRIMARY_DISPLAY_ID);
			display_id = PRIMARY_DISPLAY_ID;
			fbdev_output_flush(&b->output->base);
			weston_output_disable(&b->output->base);
			usleep(100 * 1000);
			b->secondary_connected = false;
			weston_log("HDMI is disconnected\n");
		}

		if (connected) {
			switch_display(PRIMARY_DISPLAY_ID, SECONDARY_DISPLAY_ID,
						&b->output->base, b, SECONDARY_DISPLAY_NODE, first_time);
			weston_compositor_schedule_repaint(b->compositor);
			b->secondary_connected = true;
			first_time = false;
			weston_log("HDMI is connected\n");
		}
	}
	udev_device_unref(dev);

	return 1;
}

static bool
ReadHDMISysfs() {
	int fd = open(HDMI_SYSFS_NODE_CONNECTED, O_RDONLY);
	if (fd < 0) {
		weston_log(" %s node open failed.\n",HDMI_SYSFS_NODE_CONNECTED);
	}
	char line[32];
	int read = pread(fd, line, sizeof(line),0);
	int connected = atoi(line);
	weston_log("HDMI connected = %d \n",connected);
	close(fd);
	return connected ? true : false;
}

static int
udev_event_is_hotplug(struct fbdev_backend *backend, struct udev_device *dev)
{
	const char *devname;
	const char *devpath;

	devpath = udev_device_get_devpath(dev);
	devname = udev_device_get_sysname(dev);

	int hpd = (strcmp(devpath, HDMI_SYSFS_NODE) == 0) &&
			  (strcmp(devname, "fb1")==0);
	if(hpd) {
		weston_log("HPD received \n");
		return 1;
	}
	return 0;
}

static int
ion_open()
{
	int ion_fd = open("/dev/ion", O_RDWR | O_CLOEXEC);
	if (ion_fd < 0) {
		weston_log(" Ion node open failed.\n");
	}
	return ion_fd;
}

static void fbdev_set_backlight(struct weston_output *output_base, uint32_t value)
{
	if (value > 255)
		return;

	SetBrightness(value);
}

static int fbdev_get_backlight()
{
	int brightness = 0;
	GetBrightness(&brightness);
	return brightness;

}

