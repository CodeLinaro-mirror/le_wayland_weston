/*
 * Copyright (c) 2016-2017 The Linux Foundation. All rights reserved.
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


#ifndef __SDM_STRATEGY_PLUGIN_H__
#define __SDM_STRATEGY_PLUGIN_H__

#include <pthread.h>

#include <utils/constants.h>
#include <core/display_interface.h>
#include <private/extension_interface.h>

#include <strategy_impl.h>
#include <partial_update_impl.h>
#include <resource_impl.h>

#include "plugin_buffer_sync_handler.h"
#include "sdm_strategy_plugin_interface.h"

/* Below definition should be compatible with SDM */
#define PLUGIN_MIN_ALLOC_MEMORY_SIZE	1024
#define PLUGIN_MAX_THERMAL_LEVEL	3

/*
 * Context per display. Mainly used for communicating with SDM strategy manager
 * and resource manager
 */
struct StrategyContext {
	sdm::StrategyImpl *strategy_impl;
	sdm::PartialUpdateImpl *partial_update_impl;
	uint32_t max_strategies;
	bool start_success;
	bool tried_default;
};

struct DisplayContext {
	/* Enum type. Defined and used by SDM */
	sdm::DisplayType type;
	/* SDM strategy relative classes per display */
	struct StrategyContext strategy_ctx;
	/* Constrain used by strategy. Defined in SDM */
	sdm::StrategyConstraints constraints;
	/*
	 * The pointer of DisplayResourceContext returned by
	 * ResourceImpl::RegisterDisplay
	 */
	sdm::Handle disp_res_ctx;

	/*
	 * States can be changed by user. Updating of some properties may refresh
	 * some below value
	 */
	bool idle_fallback;
	bool fallback;
	bool partial_update_enable;
	uint32_t max_layers;
};

/* This structure stores necessary information used by each display */
struct Display {
	/* Created in CreateDisplay, and destroyed by DestroyDisplay */
	DisplayContext ctx;
	/*
	 * Display and panel information which are used for initializing strategy
	 * manager and resource manager
	 */
	sdm::HWDisplayAttributes attribs;
	sdm::HWPanelInfo panel;
	sdm::HWMixerAttributes mixer_attributes;
	sdm::DisplayConfigVariableInfo fb_config;
	sdm::HWLayers hw_layers;
	sdm::LayerStack layer_stack;
};

/*
 * Property state. Store all properties set by user last time.
 * Dirty bits indicate which type of properties are updated by user
 */
struct GlobalProperty {
	/*
	 * A bit-wise mask which indicates how many types of global properties
	 * are already updated. All the global property masks are declared in
	 * sdm_strategy_plugin_interface.h. 0 means nothing is updated.
	 * It will be cleared to 0 if all cached properties are flushed to SDM
	 */
	uint32_t dirty_bits;
	uint32_t max_bw_mode;
	struct HWResourceConfig hw_res;
};

struct DisplayProperty {
	/*
	 * A bit-wise mask which indicates how many types of display properties
	 * are already updated. All the display property masks are declared in
	 * sdm_strategy_plugin_interface.h. 0 means nothing is updated.
	 * It will be cleared to 0 if all cached properties are flushed to SDM
	 */
	uint32_t dirty_bits;
	struct PluginDisplayAttributes attribs;
	struct PluginPanelInfo panel;
	struct PluginLayersConfig layer_configs;
	uint32_t thermal_level;
	uint32_t max_mixer_stages;
	bool partial_update_enable;
};

struct PropertyState {
	struct GlobalProperty global_props;
	struct DisplayProperty disp_props[PLUGIN_MAX_DISPLAYS];
};

/*
 * A client plays a role of managing all displays created by this client and
 * maintaining the property state set by this client.
 */
struct StrategyClient {
	/*
	 * Arrary for recording the created Display structure. The maximal number of
	 * display which can be created by a client is limited to PLUGIN_MAX_DISPLAYS.
	 */
	struct Display *displays[PLUGIN_MAX_DISPLAYS];
	/* Cache property state set by client */
	struct PropertyState prop_state;
};

/* This is used for managing all created clients */
struct StrategyClientContext {
	/*
	 * Record how many clients user has been created. One client must be created
	 * at least. The maximal number of client is limited to MAX_STRATEGY_CLIENTS
	 */
	uint32_t count;
	struct StrategyClient *clients[MAX_STRATEGY_CLIENTS];
};

/*
 * A global structure which is used for storing necessary resources created in Init
 * function and managing all displays and clients.
 */
struct StrategyPlugin {
	/* It's forbidden to access plugin if it's not initialized */
	bool is_init;

	/*
	 * Compositor capability information. Updated by compositor, see
	 * sdm_strategy_plugin_interface.h for more information
	 */
	uint32_t compositor_caps;

	pthread_mutex_t mutex;
	/* Internal use for SDM resource manager */
	PluginBufferSyncHandler *buf_sync_handler;
	/* Resource information used by strategy manager and resource manager */
	sdm::HWResourceInfo hw_res_info;
	/* The return pointer by SDM CreateResourceExtn function */
	sdm::ResourceImpl *res_impl;

	/* A bit-wise mask records the created display, each type of display will occupy one bit */
	uint32_t displays_mask;
	struct StrategyClientContext client_ctx;

public:
	StrategyPlugin() { pthread_mutex_init(&mutex, NULL); }
	~StrategyPlugin() { pthread_mutex_destroy(&mutex); }
};

#endif /* __SDM_STRATEGY_PLUGIN_H__ */
