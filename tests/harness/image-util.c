/*
 * Copyright © 2012 Intel Corporation
 * Copyright © 2015 Samsung Electronics Co., Ltd
 * Copyright 2016, 2017 Collabora, Ltd.
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

#include "weston-test-client-helper.h"
#include "weston-test-assert.h"
#include "image-iter.h"

#include <cairo.h>

#define max(a, b) (((a) > (b)) ? (a) : (b))
#define min(a, b) (((a) > (b)) ? (b) : (a))
#define clip(x, a, b)  min(max(x, a), b)

struct format_map_entry {
	cairo_format_t cairo;
	pixman_format_code_t pixman;
};

static const struct format_map_entry format_map[] = {
	{ CAIRO_FORMAT_ARGB32,    PIXMAN_a8r8g8b8 },
	{ CAIRO_FORMAT_RGB24,     PIXMAN_x8r8g8b8 },
	{ CAIRO_FORMAT_A8,        PIXMAN_a8 },
	{ CAIRO_FORMAT_RGB16_565, PIXMAN_r5g6b5 },
};

static pixman_format_code_t
format_cairo2pixman(cairo_format_t fmt)
{
	unsigned i;

	for (i = 0; i < ARRAY_LENGTH(format_map); i++)
		if (format_map[i].cairo == fmt)
			return format_map[i].pixman;

	test_assert_not_reached("unknown Cairo pixel format");

	return 0;
}

static cairo_format_t
format_pixman2cairo(pixman_format_code_t fmt)
{
	unsigned i;

	for (i = 0; i < ARRAY_LENGTH(format_map); i++)
		if (format_map[i].pixman == fmt)
			return format_map[i].cairo;

	test_assert_not_reached("unknown Pixman pixel format");

	return 0;
}

/**
 * Validate range
 *
 * \param r Range to validate or NULL.
 * \return The given range, or {0, 0} for NULL.
 *
 * Will abort if range is invalid, that is a > b.
 */
static struct range
range_get(const struct range *r)
{
	if (!r)
		return (struct range){ 0, 0 };

	test_assert_int_le(r->a, r->b);
	return *r;
}

/**
 * Compute the ROI for image comparisons
 *
 * \param ih_a A header for an image.
 * \param ih_b A header for another image.
 * \param clip_rect Explicit ROI, or NULL for using the whole
 * image area.
 *
 * \return The region of interest (ROI) that is guaranteed to be inside both
 * images.
 *
 * If clip_rect is given, it must fall inside of both images.
 * If clip_rect is NULL, the images must be of the same size.
 * If any precondition is violated, this function aborts with an error.
 *
 * The ROI is given as pixman_box32_t, where x2,y2 are non-inclusive.
 */
static pixman_box32_t
image_check_get_roi(const struct image_header *ih_a,
		    const struct image_header *ih_b,
		    const struct rectangle *clip_rect)
{
	pixman_box32_t box;

	if (clip_rect) {
		box.x1 = clip_rect->x;
		box.y1 = clip_rect->y;
		box.x2 = clip_rect->x + clip_rect->width;
		box.y2 = clip_rect->y + clip_rect->height;
	} else {
		box.x1 = 0;
		box.y1 = 0;
		box.x2 = max(ih_a->width, ih_b->width);
		box.y2 = max(ih_a->height, ih_b->height);
	}

	test_assert_s32_ge(box.x1, 0);
	test_assert_s32_ge(box.y1, 0);
	test_assert_s32_gt(box.x2, box.x1);
	test_assert_s32_gt(box.y2, box.y1);
	test_assert_s32_le(box.x2, ih_a->width);
	test_assert_s32_le(box.x2, ih_b->width);
	test_assert_s32_le(box.y2, ih_a->height);
	test_assert_s32_le(box.y2, ih_b->height);

	return box;
}

struct pixel_diff_stat {
	struct pixel_diff_stat_channel {
		int min_diff;
		int max_diff;
	} ch[4];
};

static void
testlog_pixel_diff_stat(const struct pixel_diff_stat *stat)
{
	int i;

	testlog("Image difference statistics:\n");
	for (i = 0; i < 4; i++) {
		testlog("\tch %d: [%d, %d]\n",
			i, stat->ch[i].min_diff, stat->ch[i].max_diff);
	}
}

static bool
fuzzy_match_pixels(uint32_t pix_a, uint32_t pix_b,
		   const struct range *fuzz,
		   struct pixel_diff_stat *stat)
{
	bool ret = true;
	int shift;
	int i;

	for (shift = 0, i = 0; i < 4; shift += 8, i++) {
		int val_a = (pix_a >> shift) & 0xffu;
		int val_b = (pix_b >> shift) & 0xffu;
		int d = val_b - val_a;

		stat->ch[i].min_diff = min(stat->ch[i].min_diff, d);
		stat->ch[i].max_diff = max(stat->ch[i].max_diff, d);

		if (d < fuzz->a || d > fuzz->b)
			ret = false;
	}

	return ret;
}

/**
 * Test if a given region within two images are pixel-identical
 *
 * Returns true if the two images pixel-wise identical, and false otherwise.
 *
 * \param img_a First image.
 * \param img_b Second image.
 * \param clip_rect The region of interest, or NULL for comparing the whole
 * images.
 * \param prec Per-channel allowed difference, or NULL for identical match
 * required.
 *
 * This function hard-fails if clip_rect is not inside both images. If clip_rect
 * is given, the images do not have to match in size, otherwise size mismatch
 * will be a hard failure.
 *
 * The per-pixel, per-channel difference is computed as img_b - img_a which is
 * required to be in the range [prec->a, prec->b] inclusive. The difference is
 * signed. All four channels are compared the same way, without any special
 * meaning on alpha channel.
 */
bool
check_images_match(pixman_image_t *img_a, pixman_image_t *img_b,
		   const struct rectangle *clip_rect, const struct range *prec)
{
	struct range fuzz = range_get(prec);
	struct pixel_diff_stat diffstat = {};
	struct image_header ih_a = image_header_from(img_a);
	struct image_header ih_b = image_header_from(img_b);
	pixman_box32_t box;
	int x, y;
	uint32_t *pix_a;
	uint32_t *pix_b;

	box = image_check_get_roi(&ih_a, &ih_b, clip_rect);

	for (y = box.y1; y < box.y2; y++) {
		pix_a = image_header_get_row_u32(&ih_a, y) + box.x1;
		pix_b = image_header_get_row_u32(&ih_b, y) + box.x1;

		for (x = box.x1; x < box.x2; x++) {
			if (!fuzzy_match_pixels(*pix_a, *pix_b,
						&fuzz, &diffstat))
				return false;

			pix_a++;
			pix_b++;
		}
	}

	return true;
}

/**
 * Tint a color
 *
 * \param src Source pixel as x8r8g8b8.
 * \param add The tint as x8r8g8b8, x8 must be zero; r8, g8 and b8 must be
 * no greater than 0xc0 to avoid overflow to another channel.
 * \return The tinted pixel color as x8r8g8b8, x8 guaranteed to be 0xff.
 *
 * The source pixel RGB values are divided by 4, and then the tint is added.
 * To achieve colors outside of the range of src, a tint color channel must be
 * at least 0x40. (0xff / 4 = 0x3f, 0xff - 0x3f = 0xc0)
 */
static uint32_t
tint(uint32_t src, uint32_t add)
{
	uint32_t v;

	v = ((src & 0xfcfcfcfc) >> 2) | 0xff000000;

	return v + add;
}

/**
 * Create a visualization of image differences.
 *
 * \param img_a First image, which is used as the basis for the output.
 * \param img_b Second image.
 * \param clip_rect The region of interest, or NULL for comparing the whole
 * images.
 * \param prec Per-channel allowed difference, or NULL for identical match
 * required.
 * \return A new image with the differences highlighted.
 *
 * Regions outside of the region of interest are shaded with black, matching
 * pixels are shaded with green, and differing pixels are shaded with
 * bright red.
 *
 * This function hard-fails if clip_rect is not inside both images. If clip_rect
 * is given, the images do not have to match in size, otherwise size mismatch
 * will be a hard failure.
 *
 * The per-pixel, per-channel difference is computed as img_b - img_a which is
 * required to be in the range [prec->a, prec->b] inclusive. The difference is
 * signed. All four channels are compared the same way, without any special
 * meaning on alpha channel.
 */
pixman_image_t *
visualize_image_difference(pixman_image_t *img_a, pixman_image_t *img_b,
			   const struct rectangle *clip_rect,
			   const struct range *prec)
{
	struct range fuzz = range_get(prec);
	struct pixel_diff_stat diffstat = {};
	pixman_image_t *diffimg;
	pixman_image_t *shade;
	struct image_header ih_a = image_header_from(img_a);
	struct image_header ih_b = image_header_from(img_b);
	struct image_header ih_d;
	pixman_box32_t box;
	int x, y;
	uint32_t *pix_a;
	uint32_t *pix_b;
	uint32_t *pix_d;
	pixman_color_t shade_color = { 0, 0, 0, 32768 };

	box = image_check_get_roi(&ih_a, &ih_b, clip_rect);

	diffimg = pixman_image_create_bits_no_clear(PIXMAN_x8r8g8b8,
						    ih_a.width, ih_a.height,
						    NULL, 0);
	ih_d = image_header_from(diffimg);

	/* Fill diffimg with a black-shaded copy of img_a, and then fill
	 * the clip_rect area with original img_a.
	 */
	shade = pixman_image_create_solid_fill(&shade_color);
	pixman_image_composite32(PIXMAN_OP_SRC, img_a, shade, diffimg,
				 0, 0, 0, 0, 0, 0, ih_a.width, ih_a.height);
	pixman_image_unref(shade);
	pixman_image_composite32(PIXMAN_OP_SRC, img_a, NULL, diffimg,
				 box.x1, box.y1, 0, 0, box.x1, box.y1,
				 box.x2 - box.x1, box.y2 - box.y1);

	for (y = box.y1; y < box.y2; y++) {
		pix_a = image_header_get_row_u32(&ih_a, y) + box.x1;
		pix_b = image_header_get_row_u32(&ih_b, y) + box.x1;
		pix_d = image_header_get_row_u32(&ih_d, y) + box.x1;

		for (x = box.x1; x < box.x2; x++) {
			if (fuzzy_match_pixels(*pix_a, *pix_b,
					       &fuzz, &diffstat))
				*pix_d = tint(*pix_d, 0x00008000); /* green */
			else
				*pix_d = tint(*pix_d, 0x00c00000); /* red */

			pix_a++;
			pix_b++;
			pix_d++;
		}
	}

	testlog_pixel_diff_stat(&diffstat);

	return diffimg;
}

/**
 * Write an image into a PNG file.
 *
 * \param image The image.
 * \param fname The name and path for the file.
 *
 * \returns true if successfully saved file; false otherwise.
 *
 * \note Only image formats directly supported by Cairo are accepted, not all
 * Pixman formats.
 */
bool
write_image_as_png(pixman_image_t *image, const char *fname)
{
	cairo_surface_t *cairo_surface;
	cairo_status_t status;
	struct image_header ih = image_header_from(image);
	cairo_format_t fmt = format_pixman2cairo(ih.pixman_format);

	cairo_surface = cairo_image_surface_create_for_data(ih.data, fmt,
							    ih.width, ih.height,
							    ih.stride_bytes);

	status = cairo_surface_write_to_png(cairo_surface, fname);
	if (status != CAIRO_STATUS_SUCCESS) {
		testlog("Failed to save image '%s': %s\n", fname,
			cairo_status_to_string(status));

		return false;
	}

	cairo_surface_destroy(cairo_surface);

	return true;
}

pixman_image_t *
image_convert_to_a8r8g8b8(pixman_image_t *image)
{
	pixman_image_t *ret;
	struct image_header ih = image_header_from(image);

	if (ih.pixman_format == PIXMAN_a8r8g8b8)
		return pixman_image_ref(image);

	ret = pixman_image_create_bits_no_clear(PIXMAN_a8r8g8b8,
						ih.width, ih.height, NULL, 0);
	test_assert_ptr_not_null(ret);

	pixman_image_composite32(PIXMAN_OP_SRC, image, NULL, ret,
				 0, 0, 0, 0, 0, 0, ih.width, ih.height);

	return ret;
}

static void
destroy_cairo_surface(pixman_image_t *image, void *data)
{
	cairo_surface_t *surface = data;

	cairo_surface_destroy(surface);
}

/**
 * Load an image from a PNG file
 *
 * Reads a PNG image from disk using the given filename (and path)
 * and returns as a Pixman image. Use pixman_image_unref() to free it.
 *
 * The returned image is always in PIXMAN_a8r8g8b8 format.
 *
 * @returns Pixman image, or NULL in case of error.
 */
pixman_image_t *
load_image_from_png(const char *fname)
{
	pixman_image_t *image;
	pixman_image_t *converted;
	cairo_format_t cairo_fmt;
	pixman_format_code_t pixman_fmt;
	cairo_surface_t *reference_cairo_surface;
	cairo_status_t status;
	int width;
	int height;
	int stride;
	void *data;

	reference_cairo_surface = cairo_image_surface_create_from_png(fname);
	cairo_surface_flush(reference_cairo_surface);
	status = cairo_surface_status(reference_cairo_surface);
	if (status != CAIRO_STATUS_SUCCESS) {
		testlog("Could not open %s: %s\n", fname,
			cairo_status_to_string(status));
		cairo_surface_destroy(reference_cairo_surface);
		return NULL;
	}

	cairo_fmt = cairo_image_surface_get_format(reference_cairo_surface);
	pixman_fmt = format_cairo2pixman(cairo_fmt);

	width = cairo_image_surface_get_width(reference_cairo_surface);
	height = cairo_image_surface_get_height(reference_cairo_surface);
	stride = cairo_image_surface_get_stride(reference_cairo_surface);
	data = cairo_image_surface_get_data(reference_cairo_surface);

	/* The Cairo surface will own the data, so we keep it around. */
	image = pixman_image_create_bits_no_clear(pixman_fmt,
						  width, height, data, stride);
	test_assert_ptr_not_null(image);

	pixman_image_set_destroy_function(image, destroy_cairo_surface,
					  reference_cairo_surface);

	converted = image_convert_to_a8r8g8b8(image);
	pixman_image_unref(image);

	return converted;
}

/**
 * Fill the image with the given color
 *
 * \param image The image to write to.
 * \param color The color to use.
 */
void
fill_image_with_color(pixman_image_t *image, const pixman_color_t *color)
{
	pixman_image_t *solid;
	int width;
	int height;

	width = pixman_image_get_width(image);
	height = pixman_image_get_height(image);

	solid = pixman_image_create_solid_fill(color);
	pixman_image_composite32(PIXMAN_OP_SRC,
				 solid, /* src */
				 NULL, /* mask */
				 image, /* dst */
				 0, 0, /* src x,y */
				 0, 0, /* mask x,y */
				 0, 0, /* dst x,y */
				 width, height);
	pixman_image_unref(solid);
}

/**
 * Convert 8-bit RGB to opaque Pixman color
 *
 * \param tmp Pixman color struct to fill in.
 * \param r Red value, 0 - 255.
 * \param g Green value, 0 - 255.
 * \param b Blue value, 0 - 255.
 * \return tmp
 */
pixman_color_t *
color_rgb888(pixman_color_t *tmp, uint8_t r, uint8_t g, uint8_t b)
{
	tmp->alpha = 65535;
	tmp->red = (r << 8) + r;
	tmp->green = (g << 8) + g;
	tmp->blue = (b << 8) + b;

	return tmp;
}
