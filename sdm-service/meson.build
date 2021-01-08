if not get_option('backend-drm')
	subdir_done()
endif

dep_libsdmcore = cxx.find_library('sdmcore')
dep_libsdmutils = cxx.find_library('sdmutils')
dep_libhdr = cxx.find_library('hdr_tm')
dep_libdisplaydebug =  cxx.find_library('displaydebug')

srcs_sdm = [
	'sdm_display.cpp',
	'sdm_display_debugger.cpp',
	'sdm_display_buffer_allocator.cpp',
	'sdm_display_buffer_sync_handler.cpp',
	'sdm_display_socket_handler.cpp',
	'sdm_display_connect.cpp',
	'uevent.c',
]

deps_sdm = [
	dep_libsdmcore,
	dep_libsdmutils,
	dep_libdrmutils,
	dep_libhdr,
	dep_libdrm,
	dep_gbm,
	dep_libdisplaydebug,
	dep_libweston_private
]
deps_sdm += dependency('threads')

sdm_service = shared_library(
	'sdm-service',
	srcs_sdm,
	include_directories: common_inc,
	dependencies: deps_sdm,
	name_prefix: '',
	install: true,
	install_dir: dir_module_libweston
)
