/*
 * Copyright (c) 2017, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 *   - Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   - Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   - Neither the name of The Linux Foundation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 *IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef WESTON_GBM_BUFFER_BACKEND_H
#define WESTON_GBM_BUFFER_BACKEND_H

struct gbm_buffer {
	struct wl_resource *buffer_resource;
	struct weston_compositor *compositor;
	int32_t fd;
	int32_t metadata_fd;
	uint32_t width;
	uint32_t height;
	uint32_t format;
};

/** Advertise gbm_buffer_backend support
 *
 * Calling this initializes the gbm_buffer_backend protocol support, so that
 * the interface will be advertised to clients. Essentially it creates a
 * global. Do not call this function multiple times in the compositor's
 * lifetime. There is no way to deinit explicitly, globals will be reaped
 * when the wl_display gets destroyed.
 *
 * \param compositor The compositor to init for.
 * \return Zero on success, -1 on failure.
 */
int gbm_buffer_backend_setup(struct weston_compositor *compositor);

/** Get the gbm_buffer from a wl_buffer resource
 *
 * If the given wl_buffer resource was created through the gdb_buffer_backend
 * protocol interface, returns the gbm_buffer object. This can be used as a
 * type check for a wl_buffer.
 *
 * \param resource A wl_buffer resource.
 * \return The gbm_buffer if it exists, or NULL otherwise.
 */
struct gbm_buffer *gbm_buffer_get(struct wl_resource *resource);

#endif /* WESTON_GBM_BUFFER_BACKEND_H */

