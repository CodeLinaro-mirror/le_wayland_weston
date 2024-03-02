if not get_option('backend-sdm')
	subdir_done()
endif

srcs_drm = [
	'sdm.c',
	'fb.c',
	'modes.c',
	'kms.c',
	'state-propose.c',
	'sdm-service/sdm_display_buffer_allocator.cpp',
	'sdm-service/sdm_display_buffer_allocator.h',
	'sdm-service/sdm_display_buffer_sync_handler.cpp',
	'sdm-service/sdm_display_buffer_sync_handler.h',
	'sdm-service/sdm_display_connect.cpp',
	'sdm-service/sdm_display_connect.h',
	'sdm-service/sdm_display.cpp',
	'sdm-service/sdm_display.h',
	'sdm-service/sdm_color_mode_intf.h',
	'sdm-service/sdm_color_mode_intf_impl.h',
	'sdm-service/sdm_color_mode_intf_impl.cpp',
	'sdm-service/sdm_display_debugger.cpp',
	'sdm-service/sdm_display_debugger.h',
	'sdm-service/sdm_display_socket_handler.cpp',
	'sdm-service/sdm_display_socket_handler.h',
	'sdm-service/uevent.c',
	'sdm-service/uevent.h',
	linux_dmabuf_unstable_v1_protocol_c,
	linux_dmabuf_unstable_v1_server_protocol_h,
	gbm_buffer_backend_protocol_c,
	gbm_buffer_backend_server_protocol_h,
	presentation_time_server_protocol_h,
]

deps_drm = [
	dep_libdl,
	dep_libweston_private,
	dep_session_helper,
	dep_libdrm,
	dep_libinput_backend,
	dependency('libudev', version: '>= 136'),
	dep_property,
	dep_glib,
]

dep_gbm = dependency('gbm', required: false)
if not dep_gbm.found()
	error('sdm-backend with GL renderer requires gbm which was not found. Or, you can use \'-Drenderer-gl=false\'.')
endif
deps_drm += dep_gbm
srcs_drm += 'sdm-gbm.c'
config_h.set('BUILD_DRM_GBM', '1')

if get_option('disable-power-key')
	config_h.set('DISABLE_POWER_KEY', '1')
endif

plugin_drm = shared_library(
	'sdm-backend',
	srcs_drm,
	include_directories: common_inc,
	c_args : ['-Wall'],
	cpp_args : ['-Wall'],
	dependencies: deps_drm,
	name_prefix: '',
	install: true,
	install_dir: dir_module_libweston
)
env_modmap += 'sdm-backend.so=@0@;'.format(plugin_drm.full_path())

install_headers(backend_drm_h, subdir: dir_include_libweston_install)
