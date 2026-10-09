#!/usr/bin/env bash
# Build the Def Jam: Fight for NY (Xbox) recompilation as a Switch homebrew NRO and stage an
# SD-card layout:
#
#   <SD>/switch/defjamx/defjamx.nro
#   <SD>/switch/defjamx/game/        the extracted disc: default.xbe, assets/, audio/, ...
#   <SD>/switch/defjamx/save/        created on first run
#   <SD>/switch/defjamx/defjamx_env.txt   optional KEY=VALUE runtime switches
#   <SD>/switch/defjamx/defjamx_log.txt   written when no nxlink host is listening
#
# The game reads the unpacked disc files directly -- never the ISO.
#
# Environment:
#   DEVKITPRO        default /opt/devkitpro (switch-dev, switch-sdl2, switch-mesa)
#   XBOXRECOMP_DIR   toolkit (default: the vendored xboxrecomp/)
#   DEFJAM_GEN_DIR    lifted C from tools/regen.sh (default $REPO/gen)
#   DEFJAM_GAME_SRC   extracted disc to stage (default $REPO/game)
#   SD_ROOT          staging SD root (default <repo>/switch_sd)
#   STAGE_GAME=0     stage only the NRO, not the 2.9 GB disc
#   BUILD_DIR        default $REPO/build-switch
#   JOBS             parallel compile jobs (default: nproc, capped by memory)
#   VULKAN=1         the Vulkan renderer: stages defjamx-vulkan.nro, built in
#                    BUILD_DIR (default $REPO/build-switch-vk) against
#                    NVK_SDK (mesa-switch install, default $REPO/../mesa-sdk/usr/local)
#                    and GLSLANG_DIR (glslang for the Switch, default $REPO/../glslang-switch)
#   LTO=1            link-time optimisation of the lifted code (DEFJAM_LTO):
#                    BUILD_DIR gets a -lto suffix and the NRO is staged as
#                    defjamx[-vulkan]-lto.nro next to the normal one, for A/B.
#                    LTO_JOBS parallel link jobs (default 4, ~2 GB each)
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
TK="${XBOXRECOMP_DIR:-$REPO/xboxrecomp}"
GEN="${DEFJAM_GEN_DIR:-$REPO/gen}"
GAME="${DEFJAM_GAME_SRC:-$REPO/game}"
SD="${SD_ROOT:-$REPO/switch_sd}"
VK="${VULKAN:-0}"
if [ "$VK" = "1" ]; then
    BUILD="${BUILD_DIR:-$REPO/build-switch-vk}"
    NRO_NAME=defjamx-vulkan.nro
    NVK_SDK="${NVK_SDK:-$REPO/../mesa-sdk/usr/local}"
    GLSLANG_DIR="${GLSLANG_DIR:-$REPO/../glslang-switch}"
    VK_ARGS=(-DDEFJAM_VULKAN=ON -DNVK_SDK="$NVK_SDK"
             -Dglslang_DIR="$GLSLANG_DIR/lib/cmake/glslang")
else
    BUILD="${BUILD_DIR:-$REPO/build-switch}"
    NRO_NAME=defjamx.nro
    VK_ARGS=(-DDEFJAM_VULKAN=OFF)
fi

LTO_ARGS=(-DDEFJAM_LTO=OFF)
if [ "${LTO:-0}" = "1" ]; then
    [ -z "${BUILD_DIR:-}" ] && BUILD="$BUILD-lto"
    NRO_NAME="${NRO_NAME%.nro}-lto.nro"
    LTO_ARGS=(-DDEFJAM_LTO=ON -DDEFJAM_LTO_JOBS="${LTO_JOBS:-4}")
fi

fail() { echo "error: $*" >&2; exit 1; }
[ -f "$DEVKITPRO/cmake/Switch.cmake" ] || fail "devkitPro not found at $DEVKITPRO"
[ -f "$DEVKITPRO/portlibs/switch/include/SDL2/SDL.h" ] || fail "switch-sdl2 missing"
[ -f "$GEN/recomp_funcs.h" ] || fail "no generated code in $GEN (run tools/regen.sh)"
[ -f "$GAME/default.xbe" ] || fail "no extracted disc in $GAME"

if [ -z "${JOBS:-}" ]; then
    mem_mb=$(( $(awk '/^MemAvailable:/ {print $2}' /proc/meminfo) / 1024 ))
    JOBS=$(( mem_mb / 1500 )); [ "$JOBS" -lt 1 ] && JOBS=1
    [ "$JOBS" -gt "$(nproc)" ] && JOBS=$(nproc)
fi

cmake -S "$REPO" -B "$BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DXBOXRECOMP_DIR="$TK" -DDEFJAM_GEN_DIR="$GEN" \
    -DDEFJAM_SWITCH_ICON="$REPO/assets/icon.jpg" "${VK_ARGS[@]}" \
    "${LTO_ARGS[@]}" >/dev/null
ninja -C "$BUILD" -j"$JOBS"

DEST="$SD/switch/defjamx"
mkdir -p "$DEST/game"
cp "$BUILD/defjam_recomp.nro" "$DEST/$NRO_NAME"
# The settings template, unless one is already there (it may hold changes).
[ -e "$DEST/defjamx_env.txt" ] || cp "$REPO/switch/defjamx_env.txt" "$DEST/defjamx_env.txt"
cp "$REPO/switch/LICENSES.txt" "$DEST/LICENSES.txt"
# The disc is 2.9 GB: copy only what changed. STAGE_GAME=0 skips it (copy
# game/ to the card yourself).
if [ "${STAGE_GAME:-1}" != "0" ]; then
    rsync -a --size-only "$GAME/" "$DEST/game/" --exclude '*_analysis.json'
fi
echo
echo "staged $DEST"
echo "  $NRO_NAME $(stat -c %s "$DEST/$NRO_NAME") bytes, game/ $(du -sh "$DEST/game" | cut -f1)"
echo "  copy <SD>/switch/defjamx/ to the card (or point Eden's sdmc at $SD),"
echo "  and start it from hbmenu with title takeover (hold R on a game) for full RAM."
