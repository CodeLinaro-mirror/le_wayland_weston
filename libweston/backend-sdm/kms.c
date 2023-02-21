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
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "config.h"

#include <stdint.h>
#include <libweston/libweston.h>
#include "shared/helpers.h"
#include "sdm-internal.h"
#include "pixel-formats.h"
#include "presentation-time-server-protocol.h"

extern int display_id;

struct drm_property_enum_info dpms_state_enums[] = {
	[WDRM_DPMS_STATE_OFF] = {
		.name = "Off",
	},
	[WDRM_DPMS_STATE_ON] = {
		.name = "On",
	},
	[WDRM_DPMS_STATE_STANDBY] = {
		.name = "Standby",
	},
	[WDRM_DPMS_STATE_SUSPEND] = {
		.name = "Suspend",
	},
};

struct drm_property_enum_info content_protection_enums[] = {
	[WDRM_CONTENT_PROTECTION_UNDESIRED] = {
		.name = "Undesired",
	},
	[WDRM_CONTENT_PROTECTION_DESIRED] = {
		.name = "Desired",
	},
	[WDRM_CONTENT_PROTECTION_ENABLED] = {
		.name = "Enabled",
	},
};

void
drm_output_update_msc(struct drm_output *output, unsigned int seq)
{
	uint64_t msc_hi = output->base.msc >> 32;

	if (seq < (output->base.msc & 0xffffffff))
		msc_hi++;

	output->base.msc = (msc_hi << 32) + seq;
}

int
on_drm_input(int fd, uint32_t mask, void *data)
{
	//SDM takes care of vsync
	return 1;
}

int
init_kms_caps(struct drm_backend *b)
{
	uint64_t cap;
	int ret;
	clockid_t clk_id;

	weston_log("using %s\n", b->drm.filename);

	ret = drmGetCap(b->drm.fd, DRM_CAP_TIMESTAMP_MONOTONIC, &cap);
	if (ret == 0 && cap == 1)
		clk_id = CLOCK_MONOTONIC;
	else
		clk_id = CLOCK_REALTIME;

	if (weston_compositor_set_presentation_clock(b->compositor, clk_id) < 0) {
		weston_log("Error: failed to set presentation clock %d.\n",
			   clk_id);
		return -1;
	}

	ret = drmGetCap(b->drm.fd, DRM_CAP_CURSOR_WIDTH, &cap);
	if (ret == 0)
		b->cursor_width = cap;
	else
		b->cursor_width = 64;

	ret = drmGetCap(b->drm.fd, DRM_CAP_CURSOR_HEIGHT, &cap);
	if (ret == 0)
		b->cursor_height = cap;
	else
		b->cursor_height = 64;

	if (!getenv("WESTON_DISABLE_UNIVERSAL_PLANES")) {
		ret = drmSetClientCap(b->drm.fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
		b->universal_planes = (ret == 0);
	}
	weston_log("DRM: %s universal planes\n",
		   b->universal_planes ? "supports" : "does not support");

	if (b->universal_planes && !getenv("WESTON_DISABLE_ATOMIC")) {
		ret = drmGetCap(b->drm.fd, DRM_CAP_CRTC_IN_VBLANK_EVENT, &cap);
		if (ret != 0)
			cap = 0;
		ret = drmSetClientCap(b->drm.fd, DRM_CLIENT_CAP_ATOMIC, 1);
		b->atomic_modeset = ((ret == 0) && (cap == 1));
	}
	weston_log("DRM: %s atomic modesetting\n",
		   b->atomic_modeset ? "supports" : "does not support");

	ret = drmGetCap(b->drm.fd, DRM_CAP_ADDFB2_MODIFIERS, &cap);
	if (ret == 0)
		b->fb_modifiers = cap;
	else
		b->fb_modifiers = 0;

	/*
	 * KMS support for hardware planes cannot properly synchronize
	 * without nuclear page flip. Without nuclear/atomic, hw plane
	 * and cursor plane updates would either tear or cause extra
	 * waits for vblanks which means dropping the compositor framerate
	 * to a fraction. For cursors, it's not so bad, so they are
	 * enabled.
	 */
	if (!b->atomic_modeset || getenv("WESTON_FORCE_RENDERER"))
		b->sprites_are_broken = true;

	ret = drmSetClientCap(b->drm.fd, DRM_CLIENT_CAP_ASPECT_RATIO, 1);
	b->aspect_ratio_supported = (ret == 0);
	weston_log("DRM: %s picture aspect ratio\n",
		   b->aspect_ratio_supported ? "supports" : "does not support");

	return 0;
}
