project(sdm_strategy_plugin)
cmake_minimum_required(VERSION 2.6)

set (LOCAL_PATH ${CMAKE_CURRENT_SOURCE_DIR})
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -Wall -O2")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -O2")

add_definitions(
    -DTESTC=${CMAKE_C_FLAGS}
    -DTESTCXX=${CMAKE_CXX_FLAGS}
    -Wall
    -Wno-missing-field-initializers
    -Wno-endif-labels
    -Wno-missing-braces
    -Wno-unused-parameter
    -std=c++11
    -fPIC
)

add_library (sdm_strategy_plugin SHARED
    ${LOCAL_PATH}/plugin_buffer_sync_handler.cpp
    ${LOCAL_PATH}/sdm_strategy_plugin.cpp
)

include_directories (${LOCAL_PATH})
include_directories (${SYSROOTINC_PATH})
include_directories (${SDM_PUBLIC_HEADER_INC})
include_directories (${SDM_HEADER_INC})
include_directories (${SCALAR_HEADER_INC})

link_directories(${SYSROOT_LIBDIR})

target_link_libraries (sdm_strategy_plugin stdc++)
target_link_libraries (sdm_strategy_plugin glib-2.0)
target_link_libraries (sdm_strategy_plugin sdmextension)
target_link_libraries (sdm_strategy_plugin pthread)

install (TARGETS sdm_strategy_plugin LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
