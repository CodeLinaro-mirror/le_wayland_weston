/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef SDM_COLOR_MANAGER_H
#define SDM_COLOR_MANAGER_H

#include <private/color_params.h>
#include "../libweston/pp-control.h"

/* The definitions of below sturctures are derived from color_apis_common.h */
/* The original header file resides in module display-noship */
struct disp_user_init_req {
  uint32_t flags;
} __attribute__((__packed__));

struct disp_user_init_res {
  DISPAPI_HANDLE hctx;
} __attribute__((__packed__));

struct disp_user_deinit_req {
  DISPAPI_HANDLE hctx;
  uint32_t flags;
} __attribute__((__packed__));

struct disp_user_get_global_pcc_config_req {
  DISPAPI_HANDLE hctx;
  uint32_t disp_id;
} __attribute__((__packed__));

struct disp_user_get_global_pcc_config_res {
  uint32_t enable;
  pcc_coeff_data cfg;
} __attribute__((__packed__));

struct disp_user_set_global_pcc_config_input {
  uint32_t req_id;
  uint32_t valid_params_flag;
  DISPAPI_HANDLE hctx;
  uint32_t disp_id;
  uint32_t enable;
  pcc_coeff_data cfg;

  disp_user_set_global_pcc_config_input() {
  }
  disp_user_set_global_pcc_config_input(uint32_t id, DISPAPI_HANDLE client_handle, uint32_t display,
                                       uint32_t pcc_enable, pcc_coeff_data &pcc_cfg)
      : req_id(id),
        valid_params_flag(0x01),
        hctx(client_handle),
        disp_id(display),
        enable(pcc_enable),
        cfg(pcc_cfg) {
      }
} __attribute__((__packed__));

struct disp_user_deinit{
  uint32_t req_id;
  struct disp_user_deinit_req deinit_req;
};

struct disp_user_init {
  uint32_t req_id;
  struct disp_user_init_req init_req;
};

struct disp_user_init_out {
  struct disp_user_init_res init_res;
};

struct disp_user_get_global_pcc_config_in {
  uint32_t req_id;
  struct disp_user_get_global_pcc_config_req get_global_pcc_config_req;
};

struct disp_user_get_global_pcc_config_out {
  uint32_t params;
  struct disp_user_get_global_pcc_config_res data;
};

#endif /* SDM_COLOR_MANAGER_H */