# Set minimum required version of CMake
cmake_minimum_required(VERSION 3.12)

# Include build functions from Pico SDK
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
include($ENV{PICO_EXTRAS_PATH}/external/pico_extras_import.cmake)

# Set name of project (as PROJECT_NAME) and C/C   standards
project(romemul C CXX ASM)
set(CMAKE_C_STANDARD 11)

# If not specified, use pico_w as default board type
set(BOARD_TYPE $ENV{BOARD_TYPE})
if(NOT BOARD_TYPE)
    set(BOARD_TYPE "pico_w")
endif()
set(PICO_BOARD ${BOARD_TYPE})

# Creates a pico-sdk subdirectory in our project for the libraries
pico_sdk_init()

# Tell CMake where to find the executable source file
add_executable(${PROJECT_NAME} 
        main.c
)

# Needed to include lwipopts.h properly
target_include_directories(romemul PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/
)
        
# Create map/bin/hex/uf2 files
pico_add_extra_outputs(${PROJECT_NAME})

# by default the header is generated into the build dir
pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/romemul.pio)

target_sources(${PROJECT_NAME} PRIVATE lwipopts.h)
target_sources(${PROJECT_NAME} PRIVATE hw_config.c)
target_sources(${PROJECT_NAME} PRIVATE commands.c)
target_sources(${PROJECT_NAME} PRIVATE constants.c)
target_sources(${PROJECT_NAME} PRIVATE firmware.c)
target_sources(${PROJECT_NAME} PRIVATE firmware_floppyemul.c)
target_sources(${PROJECT_NAME} PRIVATE firmware_rtcemul.c)
target_sources(${PROJECT_NAME} PRIVATE firmware_gemdrvemul.c)
target_sources(${PROJECT_NAME} PRIVATE romemul.c)
target_sources(${PROJECT_NAME} PRIVATE floppyemul.c)
target_sources(${PROJECT_NAME} PRIVATE romloader.c)
target_sources(${PROJECT_NAME} PRIVATE tprotocol.c)
target_sources(${PROJECT_NAME} PRIVATE config.c)
target_sources(${PROJECT_NAME} PRIVATE network.c)
target_sources(${PROJECT_NAME} PRIVATE filesys.c)
target_sources(${PROJECT_NAME} PRIVATE rtcemul.c)
target_sources(${PROJECT_NAME} PRIVATE gemdrvemul.c)
target_sources(${PROJECT_NAME} PRIVATE cJSON/cJSON.c)

# Tell CMake where to find other source code
add_subdirectory($ENV{FATFS_SDK_PATH}/src build)

# Link to pico_stdlib (gpio, time, etc. functions)
target_link_libraries(${PROJECT_NAME} PRIVATE
        FatFs_SPI                # for SD card access
        pico_stdlib              # for core functionality
        hardware_pio             # for GPIO
        hardware_dma             # for DMA
#        pico_cyw43_arch_none     # we need Wifi to access the GPIO, but we don't need anything else
#        pico_cyw43_arch_lwip_threadsafe_background # for lwip
        pico_cyw43_arch_lwip_poll
#        pico_multicore                # for multicore functionality
        pico_lwip_http
        )

# Link custom memmap with reserved memory for ROMs
set_target_properties(${PROJECT_NAME} PROPERTIES
        PICO_TARGET_LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/memmap_romemul.ld
)

# Use reduced space for ROMs
pico_set_binary_type(${PROJECT_NAME} romemul)

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

if(NOT RELEASE_TYPE OR RELEASE_TYPE STREQUAL "" OR RELEASE_TYPE STREQUAL "final")
        set(LATEST_RELEASE_URL "http://atarist.sidecartridge.com/version.txt")
endif()

if(RELEASE_TYPE STREQUAL "beta")
        set(LATEST_RELEASE_URL "http://atarist.sidecartridge.com/beta.txt")
endif()


if(NOT RELEASE_DATE)
        string(TIMESTAMP CURRENT_DATE_TIME "%Y-%m-%d %H:%M:%S")
        set(RELEASE_DATE ${CURRENT_DATE_TIME})
endif()

if (NOT _DEBUG)
        set(_DEBUG 1)
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
message("LATEST_RELEASE_URL: " ${LATEST_RELEASE_URL})

# Pass these values to the C compiler
add_definitions(-DRELEASE_TYPE="${RELEASE_TYPE}")
add_definitions(-DRELEASE_VERSION="${RELEASE_VERSION}")
add_definitions(-DRELEASE_DATE="${RELEASE_DATE}")
add_definitions(-DLATEST_RELEASE_URL="${LATEST_RELEASE_URL}")

# Pass the _DEBUG flag to the C compiler
add_definitions(-D_DEBUG=${_DEBUG})

# Optimization Flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}  -ffunction-sections -fdata-sections")
