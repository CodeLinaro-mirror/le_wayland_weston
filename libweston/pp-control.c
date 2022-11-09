/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "config.h"

#include <assert.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <libweston/libweston.h>
#include "pp-control-server-protocol.h"
#include "libweston-internal.h"
#include "pp-control.h"

static void
pp_control_manager_set_pcc(struct wl_client *client,
		struct wl_resource *resource, int fd, uint32_t size)
{
	struct control_manager *control_manager;
	void *input_data = NULL;
	struct control_pcc_input *pcc_input_data;
	struct drm_output *output;
	int ret = 0;
	void *p = NULL;
	uint32_t feature = DISP_USER_PRIV_SET_GLOBAL_PCC_CONFIG + DISP_USER_PRIV_OFFSET;

	control_manager = wl_resource_get_user_data(resource);
	if (!control_manager) {
		wl_resource_post_error(resource,
			WL_DISPLAY_ERROR_INVALID_OBJECT,
			"control_manager is no longer exsit");
		goto err_exit;
	}

	output = control_manager->output;
	if (!output) {
		wl_resource_post_error(resource,
			WL_DISPLAY_ERROR_INVALID_OBJECT,
			"No output is assigned to color_manager");
		goto err_exit;
	}

	if (size != sizeof(struct pcc_coeff_data)) {
		wl_resource_post_error(resource,
			CONTROL_MANAGER_ERROR_INVALID_SIZE,
			"User passed size = %d, expected %d", size, sizeof(struct pcc_coeff_data));
		goto err_exit;
	}

	input_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (input_data == MAP_FAILED) {
		wl_resource_post_error(resource,
			CONTROL_MANAGER_ERROR_INVALID_FD,
			"User passed invalid fd");
		goto err_exit;
	}

	pcc_input_data = zalloc(sizeof(struct control_pcc_input));
	if (pcc_input_data == NULL) {
		wl_client_post_no_memory(client);
		goto err_unmap;
	}

	pcc_input_data->handle = control_manager->handle;

	p = (void *)(&pcc_input_data->cfg);
	memcpy(p, input_data, size);

	ret = output->color_control(output, feature, (void *)pcc_input_data, NULL);
	if (ret) {
		wl_resource_post_error(resource,
			WL_DISPLAY_ERROR_INVALID_METHOD,
			"Failed to set PCC config");
		goto err_failed;
	}

	weston_output_schedule_repaint(&output->base);

err_failed:
	free(pcc_input_data);
err_unmap:
	munmap(input_data, size);
err_exit:
	if (fd > -1)
		close(fd);
}

static void
pp_control_manager_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct control_manager_interface pp_control_manager_implementation = {
	pp_control_manager_destroy,
	pp_control_manager_set_pcc,
};

static void
color_apis_deinit(struct drm_output *output, DISPAPI_HANDLE handle,
		struct wl_resource *resource)
{
	struct control_deinit_input input_params;
	uint32_t feature = DISP_USER_DEINIT + DISP_USER_OFFSET;

	input_params.handle = handle;
	input_params.flags = kDisplaySDK;

	int ret = output->color_control(output, feature, &input_params, NULL);
	if (ret) {
		wl_resource_post_error(resource,
			WL_DISPLAY_ERROR_INVALID_METHOD,
			"Failed to deinitialize display color APIs");
	}
}

static void
destroy_color_manager_data(struct wl_resource *resource)
{
	struct control_manager *control_manager;
	struct drm_output *output;

	control_manager = wl_resource_get_user_data(resource);

	if (control_manager) {
		output = control_manager->output;
		if (output)
			color_apis_deinit(output, control_manager->handle, resource);
		free(control_manager);
	}
}

static void
pp_control_create_manager(struct wl_client *client, struct wl_resource *resource,
		struct wl_resource *output_resource, uint32_t id)
{
	struct pp_control *pp_control;
	uint32_t version;
	struct control_init_output output_params;
	struct control_init_input input_params;
	struct control_manager *control_manager;
	struct weston_head *head = weston_head_from_resource(output_resource);
	struct drm_output *output = container_of(head->output, struct drm_output, base);
	uint32_t feature = DISP_USER_INIT + DISP_USER_OFFSET;

	pp_control = wl_resource_get_user_data(resource);
	if (!pp_control)
		return;

	version = wl_resource_get_version(resource);

	if (!output) {
		wl_resource_post_error(resource,
			WL_DISPLAY_ERROR_INVALID_OBJECT,
			"No output is assigned to color_manager");
		return;
	}

	if ((1 << output->base.id) & pp_control->output_mask) {
			wl_resource_post_error(resource,
				WL_DISPLAY_ERROR_INVALID_OBJECT,
				"Output has been opened by the same client again");
			return;
	}

	pp_control->output_mask |= 1 << output->base.id;
	/* Indicates sdm color manager client type */
	input_params.flags = kDisplaySDK;

	int ret = output->color_control(output, feature,
				(void*)&input_params, (void*)&output_params);
	if (ret) {
		wl_resource_post_error(resource,
			WL_DISPLAY_ERROR_INVALID_METHOD,
			"Failed to initialize display color APIs, ret = %d", ret);
			return;
	}

	control_manager = zalloc(sizeof *control_manager);
	if (control_manager == NULL) {
		color_apis_deinit(output, output_params.handle, resource);
		wl_client_post_no_memory(client);
		return;
	}

	control_manager->output = output;
	control_manager->handle = output_params.handle;

	control_manager->manager_resource = wl_resource_create(client,
				&control_manager_interface, version, id);
	if (control_manager->manager_resource == NULL) {
		color_apis_deinit(output, output_params.handle, resource);
		free(control_manager);
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(control_manager->manager_resource,
			&pp_control_manager_implementation,
			control_manager, destroy_color_manager_data);
}

static void
pp_control_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct pp_control_interface pp_control_implementation = {
	pp_control_create_manager,
	pp_control_destroy
};

static void
destroy_pp_control_data(struct wl_resource *resource)
{
	struct pp_control *pp_control;

	pp_control = wl_resource_get_user_data(resource);

	if (pp_control)
		free(pp_control);
}

static void
bind_pp_control(struct wl_client *client,
		  void *data, uint32_t version, uint32_t id)
{
	struct weston_compositor *compositor = data;
	struct pp_control *pp_control = NULL;

	pp_control = zalloc(sizeof *pp_control);
	if (!pp_control) {
		wl_client_post_no_memory(client);
		return;
	}

	pp_control->output_mask = 0;
	pp_control->compositor = compositor;
	pp_control->resource = wl_resource_create(client,
					&pp_control_interface, version, id);
	if (pp_control->resource == NULL) {
		free(pp_control);
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(pp_control->resource,
				&pp_control_implementation,
				pp_control, destroy_pp_control_data);
}

WL_EXPORT int
pp_control_setup(struct weston_compositor *compositor)
{
	if (!wl_global_create(compositor->wl_display,
			      &pp_control_interface, 1,
			      compositor, bind_pp_control))
		return -1;

	return 0;
}