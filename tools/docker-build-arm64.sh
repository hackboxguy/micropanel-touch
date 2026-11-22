#!/bin/bash
# Build (and test) the aarch64 binaries in an arm64 Debian trixie container -
# the same distribution, compiler and libraries as Raspberry Pi OS trixie -
# without root or a base image. Needs docker and qemu-user binfmt for aarch64
# (Arch: qemu-user-static + qemu-user-static-binfmt).
#
#   tools/docker-build-arm64.sh [--tests] [--clean]
#
# Output: build-arm64-docker/micropanel-touch (+ handlers, screens from the tree).
# tools/cross-build.sh (qemu chroot of the release base image) stays the
# reference for release-identical builds; this is the no-root development path.
set -euo pipefail
source_dir=$(cd "$(dirname "$0")/.." && pwd)
build_dir=build-arm64-docker
image=micropanel-touch-builder:trixie-arm64
tests=OFF
while [ "$#" -gt 0 ]; do
    case "$1" in
        --tests) tests=ON ;;
        --clean) rm -rf "${source_dir:?}/$build_dir" ;;
        -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
    shift
done

[ -f "$source_dir/external/lvgl/CMakeLists.txt" ] || git -C "$source_dir" submodule update --init --recursive

if ! docker image inspect "$image" >/dev/null 2>&1; then
    docker build --platform linux/arm64 -t "$image" - <<'DOCKERFILE'
FROM debian:trixie
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build pkg-config git ca-certificates \
        libdrm-dev libgpiod-dev libssl-dev nlohmann-json3-dev \
    && rm -rf /var/lib/apt/lists/*
DOCKERFILE
fi

docker run --rm --platform linux/arm64 --user "$(id -u):$(id -g)" \
    -v "$source_dir:/src" -w /src "$image" bash -c "
        set -e
        cmake -S . -B $build_dir -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=$tests
        cmake --build $build_dir
        if [ $tests = ON ]; then ctest --test-dir $build_dir --output-on-failure; fi
        file $build_dir/micropanel-touch 2>/dev/null || true"
echo ">> $source_dir/$build_dir/micropanel-touch"
