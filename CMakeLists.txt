zephyr_library()

execute_process(
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_architecture.py
  COMMAND_ERROR_IS_FATAL ANY
)
zephyr_library_include_directories(include)
zephyr_library_sources_ifdef(CONFIG_CARDPUTER_SD_LOG src/sd_log.c)
zephyr_library_sources_ifdef(CONFIG_CARDPUTER_LOG_CONSOLE src/log_console.c)
if(CONFIG_CARDPUTER_SD_LOG)
  zephyr_library_link_libraries(ELMFAT)
endif()
zephyr_library_sources_ifdef(CONFIG_CARDPUTER_TCA8418 src/kscan_tca8418.c)
zephyr_library_sources_ifdef(CONFIG_CARDPUTER_UI
  src/ui.c
  src/ui_render.c
  src/battery.c
  src/battery_trend.c
  src/screen.c
  src/screen_state.c
  src/behavior_screen.c
)
# This module uses the public ZMK event/profile APIs.
zephyr_library_include_directories(${APPLICATION_SOURCE_DIR}/include)
if(CONFIG_CARDPUTER_USB_HOST)
  if(NOT ADV_TINYUSB_DIR)
    message(FATAL_ERROR "Set ADV_TINYUSB_DIR to the prepared TinyUSB 0.21.0 source")
  endif()
  zephyr_library_include_directories(${ADV_TINYUSB_DIR}/src)
  zephyr_library_sources(
    src/usb_host.c
    src/usb_input.c
    src/hid_report.c
    src/behavior_usb_host.c
    src/usb/board.c
    src/usb/diagnostics.c
    src/usb/session.c
    src/usb/transport.c
    ${ADV_TINYUSB_DIR}/src/tusb.c
    ${ADV_TINYUSB_DIR}/src/common/tusb_fifo.c
    ${ADV_TINYUSB_DIR}/src/host/usbh.c
    ${ADV_TINYUSB_DIR}/src/class/hid/hid_host.c
    ${ADV_TINYUSB_DIR}/src/portable/synopsys/dwc2/dwc2_common.c
    ${ADV_TINYUSB_DIR}/src/portable/synopsys/dwc2/hcd_dwc2.c)
  # The source is hash-checked and adapted by prepare_usb.py.
  zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
endif()
