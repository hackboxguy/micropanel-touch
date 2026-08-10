#!/bin/sh
# Install only the build/runtime dependencies needed by Sprint 0.  This script
# intentionally never handles SSH credentials; run it through an authenticated
# SSH session and let sudo prompt normally.
set -eu

sudo apt-get update
sudo apt-get install -y \
    cmake \
    ninja-build \
    git \
    rsync \
    pkg-config \
    libdrm-dev \
    libgpiod-dev \
    nlohmann-json3-dev \
    evtest \
    gpiod
