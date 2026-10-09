cmake_minimum_required(VERSION 3.20)

# ============================================================================
# Def Jam: Fight for NY (Xbox, NTSC-U) - Static Recompilation
#
# Hosts: Windows (MSVC, D3D11), Linux (GCC, SDL2 + OpenGL) and Nintendo
# Switch homebrew (devkitA64 + libnx, built with switch/build.sh).
# ============================================================================

project(defjam_recomp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_definitions(
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

set(XBOXRECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/xboxrecomp" CACHE PATH
    "Path to the xboxrecomp toolkit root directory")

# The lifted code is ~100 MB of C generated from the retail XBE. It is never
# committed; point this at wherever tools.recomp --gen-dir wrote it.
set(DEFJAM_GEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/gen" CACHE PATH
    "Directory holding the tools.recomp output (recomp_*.c, recomp_funcs.h)")

if(NOT EXISTS "${DEFJAM_GEN_DIR}/recomp_funcs.h")
    message(FATAL_ERROR "No generated code in ${DEFJAM_GEN_DIR}. Run tools.recomp "
                        "with --gen-dir, or pass -DDEFJAM_GEN_DIR=<dir>.")
endif()

# The Vulkan renderer (xboxrecomp nv2a_vk) instead of OpenGL. On the Switch it
# links mesa-switch's static NVK (NVK_SDK) and builds defjamx-vulkan.nro.
option(DEFJAM_VULKAN "Render with Vulkan instead of OpenGL" OFF)
set(XBOXRECOMP_VULKAN ${DEFJAM_VULKAN} CACHE BOOL "" FORCE)

add_subdirectory(${XBOXRECOMP_DIR} ${CMAKE_BINARY_DIR}/xboxrecomp)

file(GLOB RECOMP_GEN_SOURCES CONFIGURE_DEPENDS "${DEFJAM_GEN_DIR}/*.c")

set(GAME_SOURCES
    src/main.c
    src/recomp_manual.c
    src/switch_nx.c
    src/prof_linux.c
)

if(WIN32)
    add_executable(${PROJECT_NAME} WIN32 ${GAME_SOURCES} ${RECOMP_GEN_SOURCES})
else()
    add_executable(${PROJECT_NAME} ${GAME_SOURCES} ${RECOMP_GEN_SOURCES})
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE xboxrecomp)

if(DEFJAM_VULKAN)
    target_compile_definitions(${PROJECT_NAME} PRIVATE DEFJAM_VULKAN=1)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${DEFJAM_GEN_DIR}
    ${XBOXRECOMP_DIR}/include
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        d3d11 dxgi dxguid xinput winmm dbghelp)
    if(MSVC)
        target_link_options(${PROJECT_NAME} PRIVATE /LARGEADDRESSAWARE)
        target_compile_options(${PROJECT_NAME} PRIVATE /bigobj)
    endif()
else()
    # The generated chunks are huge straight-line functions; -O1 keeps GCC's
    # memory and time per chunk sane while still allocating registers. The
    # Switch's CPU is the bottleneck there, so it gets -O2.
    if(NINTENDO_SWITCH)
        set(DEFJAM_GEN_OPT "-O2" CACHE STRING "Optimisation for the lifted code")
    else()
        set(DEFJAM_GEN_OPT "-O1" CACHE STRING "Optimisation for the lifted code")
    endif()
    # DEFJAM_LTO: link-time optimisation of the lifted code and its manual
    # overrides, so lifted callers inline each other and the native leaves
    # (_ftol2, ...). The runtime and renderers are compiled as usual.
    # DEFJAM_LTO_JOBS parallel LTRANS jobs (memory: ~1-2 GB each).
    option(DEFJAM_LTO "Link-time optimisation of the lifted code" OFF)
    set(DEFJAM_LTO_JOBS "4" CACHE STRING "Parallel LTO link jobs")
    set(DEFJAM_GEN_FLAGS "${DEFJAM_GEN_OPT};-w;-fno-strict-aliasing;-fwrapv")
    if(DEFJAM_LTO)
        list(APPEND DEFJAM_GEN_FLAGS "-flto=auto")
        set_property(SOURCE src/recomp_manual.c APPEND PROPERTY
            COMPILE_OPTIONS "-flto=auto;-fno-strict-aliasing;-fwrapv")
        target_link_options(${PROJECT_NAME} PRIVATE
            "-flto=${DEFJAM_LTO_JOBS}" "-flto-partition=balanced" ${DEFJAM_GEN_OPT})
    endif()
    set_source_files_properties(${RECOMP_GEN_SOURCES} PROPERTIES
        COMPILE_OPTIONS "${DEFJAM_GEN_FLAGS}")
    target_link_libraries(${PROJECT_NAME} PRIVATE m)
endif()

# ── Nintendo Switch homebrew (devkitA64 + libnx) ────────────────
#
# Build with switch/build.sh. The NRO reads the extracted disc from
# sdmc:/switch/defjamx/game/ (never an ISO); logs and defjamx_env.txt live in
# sdmc:/switch/defjamx/.
if(NINTENDO_SWITCH)
    set(DEFJAM_SWITCH_APP_NAME "Def Jam: Fight for NY" CACHE STRING
        "Title shown in the homebrew menu")
    set(DEFJAM_SWITCH_AUTHOR "Rodrigo Ferreira" CACHE STRING "Author shown in the homebrew menu")
    # The release lives here, not in the cache: a cached default stays at
    # whatever the build directory was first configured with.
    # -DDEFJAM_SWITCH_VERSION_OVERRIDE=x sets another one.
    set(DEFJAM_SWITCH_VERSION "1.0.0")
    if(DEFJAM_SWITCH_VERSION_OVERRIDE)
        set(DEFJAM_SWITCH_VERSION "${DEFJAM_SWITCH_VERSION_OVERRIDE}")
    endif()
    set(DEFJAM_SWITCH_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.jpg" CACHE FILEPATH
        "256x256 JPEG icon (libnx default when empty)")

    if(DEFJAM_VULKAN)
        # No GL: SDL is only the audio output; NVK comes with nv2a_vk.
        target_link_libraries(${PROJECT_NAME} PRIVATE nx m)
    else()
        # switch-sdl2's GL path is EGL on switch-mesa.
        target_link_libraries(${PROJECT_NAME} PRIVATE EGL glapi drm_nouveau nx m)
    endif()
    # SDL's audio thread gets a real-time priority and audren gaps are
    # counted (switch_nx.c, "SDL's audio thread").
    target_link_options(${PROJECT_NAME} PRIVATE
        "LINKER:--wrap=SDL_SYS_SetThreadPriority"
        "LINKER:--wrap=audrvVoiceAddWaveBuf")
    set_property(SOURCE src/switch_nx.c APPEND PROPERTY
        INCLUDE_DIRECTORIES "${XBOXRECOMP_DIR}/src/apu")
    # switch_nx.c's loading screen asks EGL for the on-screen size.
    target_include_directories(${PROJECT_NAME} PRIVATE "$ENV{DEVKITPRO}/portlibs/switch/include")

    set(DEFJAM_NACP "${CMAKE_CURRENT_BINARY_DIR}/defjamx.nacp")
    nx_generate_nacp(OUTPUT "${DEFJAM_NACP}"
        NAME "${DEFJAM_SWITCH_APP_NAME}"
        AUTHOR "${DEFJAM_SWITCH_AUTHOR}"
        VERSION "${DEFJAM_SWITCH_VERSION}")
    if(DEFJAM_SWITCH_ICON)
        nx_create_nro(${PROJECT_NAME} NACP "${DEFJAM_NACP}" ICON "${DEFJAM_SWITCH_ICON}")
    else()
        nx_create_nro(${PROJECT_NAME} NACP "${DEFJAM_NACP}")
    endif()
endif()
