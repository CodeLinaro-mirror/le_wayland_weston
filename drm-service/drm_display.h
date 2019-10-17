/*
* Copyright (c) 2018-2019, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*    * Redistributions of source code must retain the above copyright
*      notice, this list of conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above
*      copyright notice, this list of conditions and the following
*      disclaimer in the documentation and/or other materials provided
*      with the distribution.
*    * Neither the name of The Linux Foundation nor the names of its
*      contributors may be used to endorse or promote products derived
*      from this software without specific prior written permission.
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

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct EarlyDisplayInfo {
  uint32_t x_pixels;          //!< Total number of pixels in X-direction on the display panel.
  uint32_t y_pixels;          //!< Total number of pixels in Y-direction on the display panel.
  uint32_t fps;               //!< Frame rate per second.
  char *name;
  bool early_enable;
  void *early_diplay_intf;
};

/* details client to obtaining master fd
* [return]: fd
*/
int early_get_drm_master(void);

/* Display initialization: Load libsdedrm.so and get DRMManager interface
 * [input]: drm fd
 * [return]: 0, success
 */
int early_drm_display_init(int drm_fd);

/* Get connector count
 * [output]: connector count
 * [return]: 0, success
 */
int early_get_connector_count(uint32_t *count);

/* register display according to order
 * [output]: Struct EarlyDisplayInfo, for weston to create output
 * [return]: 0, success
 */
int early_create_display(uint32_t order, struct EarlyDisplayInfo *dispinfo);

/* Unregister token in token_list in order not to block SDM register
 * display
 */
void early_unregister_displays(void);

/* prepare pipe for every early_layer according early_layer->view
 * [input] struct drm_output
 * return 0, success
 */
int early_prepare(struct drm_output *output);

/* early commit
 * [input] struct drm_output
 * return 0, success
 */
int early_commit(struct drm_output *output);

/*
* [input] struct early_layer *
* prepare format, fb_id and bo for early layer
* return 0, success
*/
int early_layer_prepare(struct early_layer *layer, struct drm_output *output);


void early_drm_destroy_displays(void);

/* Early display deinitialization,DRM Manager is not destroied if sdm is still using it
 * [input]: destroy
 */
void early_drm_display_deinit(bool destroy);

/*
 * destroy early displays
 * [input]: early_display_intf
 */
void early_destroy_display(void *early_display_intf) ;

/*
 * Unregister early displays in order not to block SDM register
 * [input]: early_display_intf
 */
void early_unregister_display(void *early_display_intf);

/* Get connector id
 * [input]: display id
 * [output]: connector id
 */
uint32_t early_get_connector_id(uint32_t display_id);

#ifdef __cplusplus
}
#endif

