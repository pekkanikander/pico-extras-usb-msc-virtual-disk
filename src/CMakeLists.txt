add_library(picovd INTERFACE)

# Used by default as the creation time of the virtual disk files.
string(TIMESTAMP BUILD_EPOCH "%s" UTC)
add_compile_definitions(PICOVD_BUILD_EPOCH=${BUILD_EPOCH})

target_sources(picovd INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/vd_exfat_consts.cpp
    ${CMAKE_CURRENT_LIST_DIR}/vd_exfat_dirs.cpp
    ${CMAKE_CURRENT_LIST_DIR}/vd_exfat_directory.c
    ${CMAKE_CURRENT_LIST_DIR}/vd_virtual_disk.c
    ${CMAKE_CURRENT_LIST_DIR}/vd_usb_msc_cb.c
    ${CMAKE_CURRENT_LIST_DIR}/vd_files_rp2350.c
    ${CMAKE_CURRENT_LIST_DIR}/vd_files_changing.c
    ${CMAKE_CURRENT_LIST_DIR}/stdio_ring_buffer.c
    ${CMAKE_CURRENT_LIST_DIR}/vd_files_stdout.c
)

target_include_directories(picovd INTERFACE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(picovd INTERFACE
    tinyusb_device
    tinyusb_board
    pico_time
    pico_aon_timer
)
