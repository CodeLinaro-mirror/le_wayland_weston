// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <wayland-client.h>
#include "pcc-control-client-protocol.h"

static  struct pcc_control *pcc = NULL;

uint32_t double2s3_15(double val) {
  return ((uint32_t)((int32_t)(round((val) * (1LL << 15))))) & 0x3FFFF;
}

static void
global_registry_handler(void *data, struct wl_registry *registry, uint32_t id,
                        const char *interface, uint32_t version) {
  if (strcmp(interface, "pcc_control") == 0) {
    pcc = wl_registry_bind(registry, id, &pcc_control_interface, 1);
    printf("[Client] Bound to pcc_control interface\n");
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

int main(int argc, char **argv) {
  uint32_t coeffs[9];
  struct wl_display *display;
  struct wl_registry *registry;
  struct wl_array arr;
  void *ptr;

  if (argc != 10) {
    fprintf(stderr, "Usage %s rr rg rb gr gg gb br bg bb\n", argv[0]);
    return -1;
  }

  for (int i=0; i < 9; i++) {
    coeffs[i] = double2s3_15(atof(argv[i + 1]));
    printf("coeff[%d] = %.4f -> fixed = 0x%X\n", i, atof(argv[i + 1]), coeffs[i]);
  }

  display = wl_display_connect(NULL);
  if (!display) {
    fprintf(stderr, "Can't connect to display\n");
    exit(1);
  }
  printf("connected to display\n");

  // get registry handle
  registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &registry_listener, NULL);

  wl_display_dispatch(display);
  wl_display_roundtrip(display);

  if (pcc == NULL) {
    fprintf(stderr, "Can't find pcc\n");
    return -1;
  }

  wl_array_init(&arr);
  ptr = wl_array_add(&arr, sizeof(coeffs));
  memcpy(ptr, coeffs, sizeof(coeffs));

  pcc_control_set_pcc(pcc, &arr);
  wl_display_flush(display);
  wl_display_roundtrip(display);
  wl_array_release(&arr);

  wl_display_disconnect(display);
  printf("disconnected from display\n");
  return 0;
}
