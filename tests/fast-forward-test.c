/*
 * Copyright © 2025 Collabora, Ltd.
 * Copyright (C) 2026 Amazon.com, Inc. or its affiliates
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
#include "shared/timespec-util.h"

static enum test_result_code
fixture_setup(struct weston_test_harness *harness)
{
	struct compositor_setup setup;

	compositor_setup_defaults(&setup);
	setup.renderer = WESTON_RENDERER_PIXMAN;
	setup.width = 320;
	setup.height = 240;
	setup.shell = SHELL_TEST_DESKTOP;
	setup.logging_scopes = "log,test-harness-plugin";
	setup.refresh = HIGHEST_OUTPUT_REFRESH;

	return weston_test_harness_execute_as_client(harness, &setup);
}
DECLARE_FIXTURE_SETUP(fixture_setup);

/* Ensure we can only have one fast-forward object for a surface */
static enum test_result_code
get_two_ffwd(struct wet_testsuite_data *suite_data)
{
	struct client *client;
	struct weston_fast_forward_v1 *ffwd1, *ffwd2;

	client = create_client_and_test_surface(100, 50, 100, 100);
	test_assert_ptr_not_null(client);

	ffwd1 = weston_fast_forward_manager_v1_get_fast_forward(client->weston_fast_forward_manager,
								client->surface->wl_surface);
	ffwd2 = weston_fast_forward_manager_v1_get_fast_forward(client->weston_fast_forward_manager,
								client->surface->wl_surface);
	expect_protocol_error(client, &weston_fast_forward_manager_v1_interface,
			      WESTON_FAST_FORWARD_MANAGER_V1_ERROR_ALREADY_EXISTS);
	weston_fast_forward_v1_destroy(ffwd2);
	weston_fast_forward_v1_destroy(ffwd1);
	client_destroy(client);

	return RESULT_OK;
}

/* Ensure we can get a second fast-forward for a surface if we destroy the first. */
static enum test_result_code
get_two_ffwd_safely(struct wet_testsuite_data *suite_data)
{
	struct client *client;
	struct weston_fast_forward_v1 *ffwd1, *ffwd2;

	client = create_client_and_test_surface(100, 50, 100, 100);
	test_assert_ptr_not_null(client);

	ffwd1 = weston_fast_forward_manager_v1_get_fast_forward(client->weston_fast_forward_manager,
								client->surface->wl_surface);
	weston_fast_forward_v1_destroy(ffwd1);
	ffwd2 = weston_fast_forward_manager_v1_get_fast_forward(client->weston_fast_forward_manager,
								client->surface->wl_surface);
	weston_fast_forward_v1_destroy(ffwd2);
	client_roundtrip(client);
	client_destroy(client);

	return RESULT_OK;
}

/* Ensure the appropriate error occurs for using a fast-forward object
 * associated with a destroyed surface.
 */
static enum test_result_code
use_ffwd_on_destroyed_surface(struct wet_testsuite_data *suite_data)
{
	struct client *client;
	struct weston_fast_forward_v1 *ffwd;

	client = create_client_and_test_surface(100, 50, 100, 100);
	test_assert_ptr_not_null(client);

	ffwd = weston_fast_forward_manager_v1_get_fast_forward(client->weston_fast_forward_manager,
							       client->surface->wl_surface);
	surface_destroy(client->surface);
	client->surface = NULL;

	weston_fast_forward_v1_fast_forward(ffwd);
	expect_protocol_error(client, &weston_fast_forward_v1_interface,
			      WESTON_FAST_FORWARD_V1_ERROR_SURFACE_DESTROYED);

	weston_fast_forward_v1_destroy(ffwd);
	client_destroy(client);

	return RESULT_OK;
}

static enum test_result_code
use_ffwd(struct wet_testsuite_data *suite_data)
{
	struct client *client;
	struct buffer *buf_red, *buf_green, *buf_blue;
	struct wp_commit_timer_v1 *timer;
	struct weston_fast_forward_v1 *ffwd;
	struct wp_presentation *pres;
	struct timespec future;
	uint32_t future_sec_hi, future_sec_lo, future_nsec;
	pixman_color_t red, green, blue;
	bool match;

	color_rgb888(&red, 255, 0, 0);
	color_rgb888(&green, 0, 255, 0);
	color_rgb888(&blue, 0, 0, 255);

	client = create_client_and_test_surface(100, 50, 100, 100);
	test_assert_ptr_not_null(client);

	pres = client_get_presentation(client);

	ffwd = weston_fast_forward_manager_v1_get_fast_forward(client->weston_fast_forward_manager,
							       client->surface->wl_surface);

	timer = wp_commit_timing_manager_v1_get_timer(client->commit_timing_manager,
						      client->surface->wl_surface);

	buf_blue = surface_commit_color(client, client->surface->wl_surface, &blue, 100, 100);
	client_roundtrip(client);

	/* The distant future... */
	clock_gettime(client_get_presentation_clock(client), &future);
	timespec_add_msec(&future, &future, 1000000);
	timespec_to_proto(&future, &future_sec_hi, &future_sec_lo, &future_nsec);

	wp_commit_timer_v1_set_timestamp(timer, future_sec_hi, future_sec_lo, future_nsec);
	buf_red = surface_commit_color(client, client->surface->wl_surface, &red, 100, 100);

	weston_fast_forward_v1_fast_forward(ffwd);
	wl_surface_commit(client->surface->wl_surface);

	wp_commit_timer_v1_set_timestamp(timer, future_sec_hi, future_sec_lo, future_nsec);
	buf_green = surface_commit_color(client, client->surface->wl_surface, &green, 100, 100);

	/* The red surface should be visible, because its timestamp was
	 * ignored. But the green surface will wait a long time to display.
	 */
	match = verify_screen_content(client, "ffwd_red", 0, NULL, 0, NULL,
				      NO_DECORATIONS);
	test_assert_true(match);

	weston_fast_forward_v1_fast_forward(ffwd);
	wl_surface_commit(client->surface->wl_surface);

	/* The green surface should be visible because we ignore its
	 * timestamp now.
	 */
	match = verify_screen_content(client, "ffwd_green", 0, NULL, 0, NULL,
				      NO_DECORATIONS);
	test_assert_true(match);

	wp_commit_timer_v1_destroy(timer);
	weston_fast_forward_v1_destroy(ffwd);

	buffer_destroy(buf_red);
	buffer_destroy(buf_green);
	buffer_destroy(buf_blue);
	wp_presentation_destroy(pres);
	client_destroy(client);

	return RESULT_OK;
}

DECLARE_TEST_LIST(
	TESTFN(get_two_ffwd),
	TESTFN(get_two_ffwd_safely),
	TESTFN(use_ffwd_on_destroyed_surface),
	TESTFN(use_ffwd),
);
