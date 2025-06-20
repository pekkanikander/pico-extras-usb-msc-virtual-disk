cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type") 

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(picovd-tool C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

add_subdirectory(src)

add_executable(picovd-tool 
        picovd.c
        usb_descriptors.c
        )

pico_set_program_name(picovd-tool "picovd-tool")
pico_set_program_version(picovd-tool "0.2")

# picovd-tool is meant to run from RAM
pico_set_binary_type(picovd-tool no_flash)

# Enable stdio output over USB
pico_enable_stdio_usb(picovd-tool 1)

# Include picotool internface
target_compile_definitions(picovd-tool
  PRIVATE
    PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE=1
    PICO_STDIO_USB_RESET_INTERFACE_SUPPORT_RESET_TO_BOOTSEL=1
    PICO_STDIO_USB_RESET_INTERFACE_SUPPORT_RESET_BY_VENDOR_CMD=1
    PICO_STDIO_USB_RESET_BOOTSEL_INTERFACE_DISABLE_MASK=0 # XXX HACK FIXME
)
target_sources(picovd-tool PRIVATE
    ${PICO_SDK_PATH}/src/rp2_common/pico_stdio_usb/reset_interface.c
)

# Add the standard library to the build
target_link_libraries(
    picovd-tool
    pico_stdlib
    pico_stdio_usb
    tinyusb_device
    tinyusb_board
    picovd
    )

# Add the standard include files to the build
target_include_directories(picovd-tool PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

pico_add_extra_outputs(picovd-tool)
