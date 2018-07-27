/*
* Copyright (c) 2018, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
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
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-server.h>
#include <wayland-client-protocol.h>

#include "mediabox-platform-client-protocol.h"

#define SLEEP_TIME 20

struct wl_display *display = NULL;
struct wl_mediabox_platform *actor = NULL;

static void global_registry_handler(void *data, struct wl_registry *registry,
	uint32_t id, const char *interface, uint32_t version)
{
	printf("Got a registry event for %s id %d\n", interface, id);
	if (strcmp(interface, "wl_mediabox_platform") == 0)
		actor = wl_registry_bind(registry,
				id,
				&wl_mediabox_platform_interface,
				1);
}

static void global_registry_remover(void *data,
	struct wl_registry *registry, uint32_t id)
{
	printf("Got a registry losing event for %d\n", id);
}

static const struct wl_registry_listener registry_listener = {
	global_registry_handler,
	global_registry_remover
};

int do_hpd_on_off_test(int use_key, int delay)
{
	char cmd = 'o';
	int rc = 0;
	do {
		if (use_key)
		{
			printf("Enter a command sequence to Turn On HPD (o), Turn Off HPD (f)," \
				" or Quit (q): ");
			cmd = getchar();
			if (cmd == 'q') break;
		}
		else
		{
			cmd = (cmd=='o') ? 'f' : 'o' ;
		}
		printf("Powering %s HPD Clocks ...\n", (cmd=='f') ? "Off":"On");
		wl_mediabox_platform_set_hpd(actor, (cmd=='f') ? 1:0);
		// Flush requests to server
		rc = wl_display_flush(display);
		if (rc < 0)
			fprintf(stderr, "failed to flush display\n");
		sleep(delay);

	} while (1);
}

void mode_set_with_index(int delay, int idx)
{
	printf("Setting mode index %d\n", idx);
	wl_mediabox_platform_set_mode_index(actor, idx);
	// Flush requests to server
	if (wl_display_flush(display) < 0)
		fprintf(stderr, "failed to flush display\n");
	sleep(delay);
}

int do_mode_set_test(int test_id, int use_key, int delay, int idx_a, int idx_b)
{
	if (test_id == 2)
	{
		int idx = 0;
		for (idx = idx_a; idx <= idx_b ; idx++)
			mode_set_with_index(delay, idx);
	}
	else if (test_id == 3)
	{
		while(1)
		{
			mode_set_with_index(delay, idx_a);
			mode_set_with_index(delay, idx_b);
		}
	}
}

int do_mode_set_attributes_test(int test_id, int use_key, int delay,
					uint width, uint height, uint fps, int ar)
{
	int loop = 0;
	int rc = 0;
	uint wid = 0, hght = 0, f = 0;
	int aspect_ratio = 0;

	do {
		if (use_key)
		{
			printf("Enter W:H:FPS:AR ");
			rc = scanf("%u:%u:%u:%d", &wid, &hght, &f, &aspect_ratio);
			if ( rc != 4)
				printf("\nInvalid values: rc:%d\n", rc);
			printf("\n");
			loop =1;
		}
		else
		{
			wid = width;
			hght= height;
			f = fps;
			aspect_ratio = ar;
			loop =0;
		}

		wl_mediabox_platform_set_mode_attributes(actor, wid, hght, f, aspect_ratio);
		// Flush requests to server
		rc = wl_display_flush(display);
		if (rc < 0)
			fprintf(stderr, "failed to flush display\n");
		sleep(delay);

	} while (loop);
}

/**
 * Use case examples:
 * 1. weston-mediabox-platform -d 10 -k -h
 * 2. weston-mediabox-platform -d 30 -m1 0 20
 * 3. weston-mediabox-platform -d 30 -m2 1 22
 * 4. weston-mediabox-platform -m3 1920 1080 60 -1
 * 5. weston-mediabox-platform -d 20 -k -m3 1920 1080 60 -1
 *
 */
int main(int argc, char **argv)
{
	int i = 0;
	int delay = SLEEP_TIME;
	int use_key = 0;
	int start_idx = 0;
	int end_idx = 0;
	int test_id = 0;
	uint width = 0, height= 0, fps = 0;
	int ar = 0;

	/* Need to follow the format of the specific command arg */
	for (i = 1; i < argc; i++) {
		if (strcmp("-d", argv[i]) == 0)
			delay = atoi(argv[++i]);
		if (strcmp("-k", argv[i]) == 0)
			use_key = 1;
		if (strcmp("-h", argv[i]) == 0)
			test_id = 1;
		if (strcmp("-m1", argv[i]) == 0)
		{
			test_id = 2;
			start_idx = atoi(argv[++i]);
			end_idx = atoi(argv[++i]);
		}
		if (strcmp("-m2", argv[i]) == 0)
		{
			test_id = 3;
			start_idx = atoi(argv[++i]);
			end_idx = atoi(argv[++i]);
		}
		if (strcmp("-m3", argv[i]) == 0)
		{
			test_id = 4;
			width = atoi(argv[++i]);
			height = atoi(argv[++i]);
			fps = atoi(argv[++i]);
			ar = atoi(argv[++i]);
		}
	}

	display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "failed to create display\n");
		return -1;
	}
	printf("connected to display\n");

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);

	wl_display_dispatch(display);
	wl_display_roundtrip(display);

	if (actor == NULL) {
		fprintf(stderr, "Can't find wl_mediabox_platform\n");
		return -1;
	}

switch(test_id)
{
	case 1:
		do_hpd_on_off_test(use_key, delay);
		break;
	case 2:
	case 3:
		do_mode_set_test(test_id, use_key, delay, start_idx, end_idx);
		break;
	case 4:
		do_mode_set_attributes_test(test_id, use_key, delay, width, height, fps, ar);
		break;
	default:
		printf("invalid test id %d\n", test_id);
		break;
}
	wl_display_disconnect(display);
	printf("disconnected from display\n");

	return 0;
}
