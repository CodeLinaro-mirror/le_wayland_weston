#!/bin/bash
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear

set -e

LIB_DIR="/usr/lib/aarch64-linux-gnu"

# --- Step 1: Rename existing libraries (only if not already renamed) ---

if [ -f "${LIB_DIR}/libgbm.so" ]; then
    mv "${LIB_DIR}/libgbm.so" "${LIB_DIR}/gbm.so"
    echo "setup-gpu-libs: renamed libgbm.so -> gbm.so"
fi

if [ -f "${LIB_DIR}/libEGL.so.1.1.0" ]; then
    mv "${LIB_DIR}/libEGL.so.1.1.0" "${LIB_DIR}/EGL.so.1.1.0"
    echo "setup-gpu-libs: renamed libEGL.so.1.1.0 -> EGL.so.1.1.0"
fi

if [ -f "${LIB_DIR}/libGLESv1_CM.so.1.2.0" ]; then
    mv "${LIB_DIR}/libGLESv1_CM.so.1.2.0" "${LIB_DIR}/GLESv1_CM.so.1.2.0"
    echo "setup-gpu-libs: renamed libGLESv1_CM.so.1.2.0 -> GLESv1_CM.so.1.2.0"
fi

if [ -f "${LIB_DIR}/libGLESv2.so.2.1.0" ]; then
    mv "${LIB_DIR}/libGLESv2.so.2.1.0" "${LIB_DIR}/GLESv2.so.2.1.0"
    echo "setup-gpu-libs: renamed libGLESv2.so.2.1.0 -> GLESv2.so.2.1.0"
fi

# --- Step 2: Create symlinks pointing to Adreno GPU libraries ---
# Helper: only call ln -sf when the current symlink target differs from expected.
make_symlink() {
    local link="${LIB_DIR}/$1"   # full path of the symlink
    local target="$2"             # expected target (relative name)
    if [ "$(readlink "${link}")" != "${target}" ]; then
        ln -sf "${target}" "${link}"
        echo "setup-gpu-libs: symlink ${1} -> ${target}"
    fi
}

make_symlink libEGL.so.1        libEGL_adreno.so
make_symlink libGLESv1_CM.so.1  libGLESv1_CM_adreno.so
make_symlink libGLESv2.so.2     libGLESv2_adreno.so

echo "setup-gpu-libs: GPU library setup completed successfully."
exit 0
