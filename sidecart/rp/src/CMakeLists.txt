# Set minimum required version of CMake
cmake_minimum_required(VERSION 3.26)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# if PICOTOOL_OVERRIDE_DIR system environment variable is set,
# then use that as the folder for the picotool executable
if (DEFINED ENV{PICOTOOL_OVERRIDE_DIR})
    message("PICOTOOL_OVERRIDE_DIR env var is set to '$ENV{PICOTOOL_OVERRIDE_DIR}'")
    add_executable(picotool IMPORTED GLOBAL)
    set_property(TARGET picotool PROPERTY IMPORTED_LOCATION $ENV{PICOTOOL_OVERRIDE_DIR}/picotool)
    # check the picotool version:
    execute_process(COMMAND $ENV{PICOTOOL_OVERRIDE_DIR}/picotool version
                    OUTPUT_VARIABLE PICOTOOL_VERSION
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REGEX MATCH "^picotool v${picotool_VERSION_REQUIRED}" PICOTOOL_VERSION_MATCH ${PICOTOOL_VERSION})
    if (NOT PICOTOOL_VERSION_MATCH)
        message("picotool version response was: ${PICOTOOL_VERSION}")
        message(FATAL_ERROR "PICOTOOL_OVERRIDE_DIR is set to '$ENV{PICOTOOL_OVERRIDE_DIR}', but the version of picotool found is not ${picotool_VERSION_REQUIRED}")
    else()
        message("picotool version response was: ${PICOTOOL_VERSION}")
    endif()
endif ()

# If not specified, use pico_w as default board type
set(BOARD_TYPE $ENV{BOARD_TYPE})
if(NOT BOARD_TYPE)
    set(BOARD_TYPE "pico_w")
endif()
set(PICO_BOARD ${BOARD_TYPE})

# Determine board type macros
if("${BOARD_TYPE}" STREQUAL "pico_w")
    add_compile_definitions(BOARD_TYPE_PICO_W=1 BOARD_TYPE_PICO=0 BOARD_TYPE_CUSTOM16MB=0)
elseif("${BOARD_TYPE}" STREQUAL "pico")
    add_compile_definitions(BOARD_TYPE_PICO_W=0 BOARD_TYPE_PICO=1 BOARD_TYPE_CUSTOM16MB=0)
elseif("${BOARD_TYPE}" STREQUAL "sidecartos_16mb")
    add_compile_definitions(BOARD_TYPE_PICO_W=0 BOARD_TYPE_PICO=1 BOARD_TYPE_CUSTOM16MB=0)
else()
    message(FATAL_ERROR "Unknown BOARD_TYPE: ${BOARD_TYPE}")
endif()

# Resolve bundled Pico dependencies relative to the repo root so configure
# works regardless of the active build directory.
# sidecart/ is the "repo root" for the SDK submodules; the ROTT sources the
# renderer shares (rott/atari_tables.c) live one level further up.
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." REALPATH)
get_filename_component(ROTT_ROOT "${REPO_ROOT}/.." REALPATH)
set(DEFAULT_FATFS_SDK_PATH "${REPO_ROOT}/fatfs-sdk")
set(DEFAULT_PICO_SDK_PATH "${REPO_ROOT}/pico-sdk")
set(DEFAULT_PICO_EXTRAS_PATH "${REPO_ROOT}/pico-extras")

if(NOT DEFINED ENV{FATFS_SDK_PATH} OR "$ENV{FATFS_SDK_PATH}" STREQUAL "")
    set(ENV{FATFS_SDK_PATH} "${DEFAULT_FATFS_SDK_PATH}")
endif()
set(FATFS_SDK_PATH "$ENV{FATFS_SDK_PATH}" CACHE PATH "Path to the FATFS SDK" FORCE)
get_filename_component(FATFS_SDK_PATH "${FATFS_SDK_PATH}" REALPATH BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(FATFS_SDK_PATH "${FATFS_SDK_PATH}" CACHE PATH "Path to the FATFS SDK" FORCE)
set(ENV{FATFS_SDK_PATH} "${FATFS_SDK_PATH}")
message("FATFS_SDK_PATH: ${FATFS_SDK_PATH}")

if(NOT DEFINED ENV{PICO_SDK_PATH} OR "$ENV{PICO_SDK_PATH}" STREQUAL "")
    set(ENV{PICO_SDK_PATH} "${DEFAULT_PICO_SDK_PATH}")
endif()
set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}" CACHE PATH "Path to the Raspberry Pi Pico SDK" FORCE)
get_filename_component(PICO_SDK_PATH "${PICO_SDK_PATH}" REALPATH BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(PICO_SDK_PATH "${PICO_SDK_PATH}" CACHE PATH "Path to the Raspberry Pi Pico SDK" FORCE)
set(ENV{PICO_SDK_PATH} "${PICO_SDK_PATH}")
message("PICO_SDK_PATH: ${PICO_SDK_PATH}")

if(NOT DEFINED ENV{PICO_EXTRAS_PATH} OR "$ENV{PICO_EXTRAS_PATH}" STREQUAL "")
    set(ENV{PICO_EXTRAS_PATH} "${DEFAULT_PICO_EXTRAS_PATH}")
endif()
set(PICO_EXTRAS_PATH "$ENV{PICO_EXTRAS_PATH}" CACHE PATH "Path to the PICO EXTRAS" FORCE)
get_filename_component(PICO_EXTRAS_PATH "${PICO_EXTRAS_PATH}" REALPATH BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(PICO_EXTRAS_PATH "${PICO_EXTRAS_PATH}" CACHE PATH "Path to the PICO EXTRAS" FORCE)
set(ENV{PICO_EXTRAS_PATH} "${PICO_EXTRAS_PATH}")
message("PICO_EXTRAS_PATH: ${PICO_EXTRAS_PATH}")

# FatFs is written for malloc-returns-NULL semantics (see md-doom); keep the
# SDK from panicking on its first over-sized request.
add_definitions(-DPICO_MALLOC_PANIC=0)
add_definitions(-DPICO_USE_OPTIMISTIC_SBRK=1)

# Include build functions from Pico SDK
include(${PICO_SDK_PATH}/pico_sdk_init.cmake)
include(${PICO_EXTRAS_PATH}/external/pico_extras_import.cmake)

project(rp C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_subdirectory(settings)

# Tell CMake where to find other source code
add_subdirectory(${FATFS_SDK_PATH}/src build)

# The MD/ROTT renderer: ROTT's own, ported (rott/, see r_local.h), plus the
# ST build's fixed-point tables, compiled from rott/ rather than copied.
file(GLOB ROTT_RENDERER_SOURCES ${CMAKE_CURRENT_LIST_DIR}/rott/*.c)
set(ROTT_SHARED_SOURCES ${ROTT_ROOT}/rott/atari_tables.c)
# The renderer is the hot path: -O2 rather than the global MinSizeRel.
set_source_files_properties(${ROTT_RENDERER_SOURCES} md_video.c PROPERTIES
        COMPILE_OPTIONS "-O2")

add_executable(${PROJECT_NAME}
        main.c
)

pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/romemul.pio)
pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/commemul.pio)

target_sources(${PROJECT_NAME} PRIVATE
        aconfig.c
        commemul.c
        emul.c
        gconfig.c
        hw_config.c
        md_core1.c
        md_main.c
        md_pack.c
        md_proto.c
        md_video.c
        reset.c
        romemul.c
        sdcard.c
        select.c
        tprotocol.c
        ${ROTT_RENDERER_SOURCES}
        ${ROTT_SHARED_SOURCES}
        settings/settings.c)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(${PROJECT_NAME})

# 400 MHz sysclk (constants.h) needs the QSPI flash divider raised from
# the board default /2 to /4 so flash SCK stays at 100 MHz (md-doom).
add_definitions(-DPICO_FLASH_SPI_CLKDIV=4)
target_compile_definitions(bs2_default PRIVATE PICO_FLASH_SPI_CLKDIV=4)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${LINK_LIBRARIES}
    hardware_flash
    no-OS-FatFS-SD-SDIO-SPI-RPi-Pico
    pico_stdlib
    pico_multicore
    settings
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
        PICO_STACK_SIZE=0x1000 PICO_CORE1_STACK_SIZE=0x800)

# Link custom memmap with reserved memory for ROMs
set_target_properties(${PROJECT_NAME} PROPERTIES
        PICO_TARGET_LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/memmap_rp.ld
)

# Project-local FatFs config (ff/ffconf.h) wins over the submodule's.
target_include_directories(${PROJECT_NAME} BEFORE PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/ff)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/..
    ${REPO_ROOT}/include
    ${ROTT_ROOT}/rott)

pico_set_binary_type(${PROJECT_NAME} rp)

# Fetch the values from the environment variables
set(RELEASE_TYPE $ENV{RELEASE_TYPE})
set(RELEASE_VERSION $ENV{RELEASE_VERSION})
set(RELEASE_DATE $ENV{RELEASE_DATE})
set(_DEBUG $ENV{DEBUG_MODE})

# If the environment variables are not set, use default values
if(NOT RELEASE_VERSION)

    # Check if RELEASE_TYPE is set and not empty, otherwise use default "FINAL"
    if(NOT RELEASE_TYPE OR RELEASE_TYPE STREQUAL "")
        set(RELEASE_TYPE "final")
    endif()

    # Determine the file name based on RELEASE_TYPE
    if(RELEASE_TYPE STREQUAL "final")
        set(VERSION_FILE_NAME "version.txt")
    else()
        set(VERSION_FILE_NAME "version-${RELEASE_TYPE}.txt")
    endif()
    
    # Remove newline character if present
    string(STRIP "${RELEASE_VERSION}" RELEASE_VERSION)

    # In case version.txt is empty or does not exist, use a default version
    if("${RELEASE_VERSION}" STREQUAL "")
        set(RELEASE_VERSION "v0.0.1dev")
    endif()
endif()

if(NOT RELEASE_DATE)
        string(TIMESTAMP CURRENT_DATE_TIME "%Y-%m-%d %H:%M:%S")
        set(RELEASE_DATE ${CURRENT_DATE_TIME})
endif()

# Default _DEBUG to 0 when the env var isn't set at all. `if (NOT _DEBUG)`
# would also fire for the string "0" (CMake treats "0" as falsy), silently
# turning a CMakePresets release preset into a debug build. Mirrors the
# pattern used in rp2-atarist-rpikb's CMakeLists.txt.
if ("${_DEBUG}" STREQUAL "")
        set(_DEBUG 0)
endif()

# Debug outputs
pico_enable_stdio_usb(${PROJECT_NAME} 0)
# Workaround to disable USB output in release builds
if(${_DEBUG} STREQUAL "0")
    pico_enable_stdio_uart(${PROJECT_NAME} 0)
else()
    pico_enable_stdio_uart(${PROJECT_NAME} 1)
endif()

# Print the variables on screen
message("RELEASE_TYPE: " ${RELEASE_TYPE})
message("RELEASE_VERSION: " ${RELEASE_VERSION})
message("RELEASE_DATE: " ${RELEASE_DATE})
message("DEBUG_MODE: " ${_DEBUG})

# Pass these values to the C compiler
add_definitions(-DRELEASE_TYPE="${RELEASE_TYPE}")
add_definitions(-DRELEASE_VERSION="${RELEASE_VERSION}")
add_definitions(-DRELEASE_DATE="${RELEASE_DATE}")

# Pass the APP_UUID_KEY to the C compiler
if(DEFINED ENV{APP_UUID_KEY} AND NOT "$ENV{APP_UUID_KEY}" STREQUAL "")
    message(STATUS "Using APP_UUID_KEY from environment: $ENV{APP_UUID_KEY}")
    add_definitions(-DCURRENT_APP_UUID_KEY=\"$ENV{APP_UUID_KEY}\")
else()
    message(STATUS "Using default UUID")
    add_definitions(-DCURRENT_APP_UUID_KEY=\"44444444-4444-4444-8444-444444444444\")
endif()

# Pass the _DEBUG flag to the C compiler
add_definitions(-D_DEBUG=${_DEBUG})

# Device/Computer type
add_definitions(-DDISPLAY_ATARIST)

# We don't modify the flash in Core 1
add_definitions(-DPICO_FLASH_ASSUME_CORE0_SAFE=1)


# Remove unused data
target_link_options(${PROJECT_NAME} PRIVATE
   "-Wl,--gc-sections"
   "-Wl,--strip-all"
)

# No clang-tidy: the renderer is ROTT's own code, kept close to the original
# so the two can be compared, and it does not follow .clang-tidy's naming.

# Find clang-format executable
find_program(CLANG_FORMAT_BIN NAMES clang-format)

if(CLANG_FORMAT_BIN)
    message(STATUS "Found clang-format: ${CLANG_FORMAT_BIN}")
    message(STATUS "Linting in the source dir: ${CMAKE_SOURCE_DIR}")

    # Add a custom target to run clang-format on all source files
    file(GLOB_RECURSE ALL_CXX_SOURCE_FILES
        "${CMAKE_SOURCE_DIR}/*.c"
        "${CMAKE_SOURCE_DIR}/include/*.h"
    )

    add_custom_target(clang-format
        COMMAND ${CLANG_FORMAT_BIN}
        -i
        -style=file
        ${ALL_CXX_SOURCE_FILES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/../..
        COMMENT "Running clang-format on the source code..."
        VERBATIM
    )

else()
    message(WARNING "clang-format not found!")
endif()
