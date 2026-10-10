# Copyright (c) 2021 HPMicro
# SPDX-License-Identifier: BSD-3-Clause

set(APP_NAME hpm_hello_world)

cmake_minimum_required(VERSION 3.13)

set(BOARD_SEARCH_PATH ${CMAKE_CURRENT_SOURCE_DIR}/boards)

# set(CONFIG_SEGGER_RTT 1)

# Enable CherryUSB (DFU runtime + CDC ACM VCOM)
set(CONFIG_CHERRYUSB 1)
set(CONFIG_CHERRYUSB_DEVICE 1)
set(CONFIG_USB_DEVICE 1)
set(CONFIG_USB_DEVICE_CDC_ACM 1)
# NOTE: CONFIG_USB_DEVICE_DFU is enabled in src/usb_config.h for DFU runtime only.
# The APP uses the DFU runtime interface (handled manually in src/usb_desc.c),
# not the full DFU mode (hpm_dfu_port.c).

find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE})

# CherryUSB config — pick up local usb_config.h
sdk_compile_definitions(-D__INCLUDE_CHERRYUSB_CONFIG_H__)
sdk_inc(${CMAKE_CURRENT_SOURCE_DIR})
sdk_inc(${CMAKE_CURRENT_SOURCE_DIR}/src)

# Use SDK's flash_dfu.ld — reserve 128K for bootloader
sdk_linker_global_symbols("_dfu_bl_length=0x20000")

# sdk_compile_definitions(-DCONFIG_NDEBUG_CONSOLE=1)

project(hello_world)

sdk_app_src(src/hello_world.c)
sdk_app_src(src/hpm_dfu_trigger.c)
sdk_app_src(src/usb_desc.c)

set(ELF ${CMAKE_BINARY_DIR}/output/${APP_NAME}.elf)
set(HEX ${CMAKE_BINARY_DIR}/output/${APP_NAME}.hex)

add_custom_command(OUTPUT ${HEX}
    COMMAND ${CMAKE_OBJCOPY} -O ihex -S ${ELF} ${HEX}
    DEPENDS ${ELF}
    COMMENT "Generating ${APP_NAME}.hex"
)
add_custom_target(artifacts ALL DEPENDS ${HEX})

# generate_ide_projects()
