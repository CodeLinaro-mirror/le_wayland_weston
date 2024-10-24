/*
* Copyright (c) 2021, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*  * Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
*  * Redistributions in binary form must reproduce the above
*    copyright notice, this list of conditions and the following
*    disclaimer in the documentation and/or other materials provided
*    with the distribution.
*  * Neither the name of The Linux Foundation nor the names of its
*    contributors may be used to endorse or promote products derived
*    from this software without specific prior written permission.
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
* Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include <stdio.h>
#include <stdlib.h>
#include <wayland-client.h>
#include "weston-qti-extn-client-protocol.h"

#define MAX_DIGITS 5

struct display {
  struct wl_display *display;
  struct wl_compositor *compositor;
  struct weston_qti_extn *qti_extn;
};
struct display display;

static void
global_registry_handler(void *data, struct wl_registry *registry, uint32_t id,
                        const char *interface, uint32_t version) {
  printf("Got a registry event for %s id %d\n", interface, id);
  if (strcmp(interface, "wl_compositor") == 0) {
    display.compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 1);
  } else if (strcmp(interface, "weston_qti_extn") == 0) {
    display.qti_extn = wl_registry_bind(registry, id, &weston_qti_extn_interface, 1);
  }
}

static void
global_registry_remover(void *data, struct wl_registry *registry, uint32_t id) {
  printf("Got a registry losing event for %d\n", id);
}

static const struct wl_registry_listener registry_listener = {
  global_registry_handler,
  global_registry_remover
};

long int get_integer_input() {
  long int integerinput = -1;
  char *buffer = malloc(sizeof(char) * MAX_DIGITS);
  if (!buffer) {
    printf("Failed to allocate memory\n");
    return -1;
  }

  memset(buffer, 0, MAX_DIGITS);
  char *bufferptr = fgets(buffer, MAX_DIGITS, stdin);
  if (bufferptr == NULL) {
    return integerinput;
  }

  char *end_ptr = NULL;
  integerinput = strtol(bufferptr, &end_ptr, 10);
  if (bufferptr + strlen(buffer) -1 != end_ptr) {
    printf("Not an Integer\n");
    return -1;
  }

  return integerinput;
}

int main(int argc, char **argv) {
  display.display = wl_display_connect(NULL);
  if (display.display == NULL) {
    fprintf(stderr, "Can't connect to display\n");
    exit(1);
  }
  printf("connected to display\n");

  // get registry handle
  struct wl_registry *registry = wl_display_get_registry(display.display);
  wl_registry_add_listener(registry, &registry_listener, NULL);

  wl_display_dispatch(display.display);
  wl_display_roundtrip(display.display);

  if (display.compositor == NULL) {
    fprintf(stderr, "Can't find compositor\n");
    exit(1);
  }

  if (display.qti_extn == NULL) {
    fprintf(stderr, "Can't find weston_qti_extn\n");
    exit(1);
  }

  printf("Enter the test case no : \n \
            1. Power On \n \
            2. Power Off \n \
            3. set brightness \n \
            4. Exit \n");
  printf("enter your choice : ");
  long int choice = get_integer_input();
  int loop = 1;
  while (1) {
    switch (choice) {
      case 1:
        weston_qti_extn_power_on(display.qti_extn);
      break;
      case 2:
        weston_qti_extn_power_off(display.qti_extn);
      break;
      case 3:
        printf("Enter brightness value : ");
        uint32_t value = (uint32_t) get_integer_input();
        weston_qti_extn_set_brightness(display.qti_extn, value);
      break;
      case 4:
        loop = 0;
      break;
      default :
        printf("wrong choice Enter again\n");
      break;
    }
    if (loop == 0) {
      break;
    }

    wl_display_roundtrip(display.display);

    printf("Enter the test case no : \n \
              1. Power On \n \
              2. Power Off \n \
              3. set brightness \n \
              4. Exit \n");
    printf("enter your choice : ");
    choice = get_integer_input();
  }

  wl_display_disconnect(display.display);
  printf("disconnected from display\n");

  exit(0);
}
