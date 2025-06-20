add_library(picovd INTERFACE)

target_sources(picovd INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/vd_exfat_consts.cpp
    ${CMAKE_CURRENT_LIST_DIR}/vd_exfat_dirs.cpp
    ${CMAKE_CURRENT_LIST_DIR}/vd_exfat_directory.c
    ${CMAKE_CURRENT_LIST_DIR}/vd_virtual_disk.c
    ${CMAKE_CURRENT_LIST_DIR}/vd_rp2350.c
)

target_include_directories(picovd INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(picovd INTERFACE 
    tinyusb_device
)

