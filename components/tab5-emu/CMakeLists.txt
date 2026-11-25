idf_component_register(
  INCLUDE_DIRS "include"
  SRCS
    "src/tab5-emu.cpp" "src/usb_device.cpp" "src/usb_msc_gate.cpp" "src/usb_hid.cpp" "src/usb_restart.c"
    "src/ext_ram_data.c" "src/overlay.c" "src/lv_mem_core_psram.c" "src/make_color.cpp"
    "src/coredump.cpp" "src/input_profiles_hal.cpp" "src/paludo_platform.cpp"
    "src/wall_clock.cpp" "src/audio_probe.cpp" "src/inactivity.cpp" "src/touch_controls.cpp" "src/content_storage.cpp" "src/keyboard_i2c.cpp"
    "input_profiles/input_profiles.cpp" "storage/battery_store.cpp" "storage/atomic_write.cpp" "storage/file_read.cpp"
  LDFRAGMENTS "linker.lf"
  REQUIRES
  "driver"
  "app_update"
  "heap"
  "fatfs"
  "esp_lcd"
  "esp_psram"
  "esp_driver_ppa"
  "esp_mm"
  "hal"
  "lvgl"
  "codec"
  "display"
  "display_drivers"
  "input_drivers"
  "gt911"
  "event_manager"
  "pool_allocator"
  "task"
  "timer"
  "m5stack-tab5"
  "statistics"
  "usb_host"
  "hid-rp"
  "usb_device"
  "esp_hal_usb"
  "coredump"
  "mbedtls"
  "sdcard"
  "cjson"
  )

# LVGL's custom allocator lives in src/lv_mem_core_psram.c; liblvgl is linked
# after this library, so pull the object in explicitly
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u lv_mem_init")
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_restart_noos" "-Wl,--wrap=xStreamBufferReceive" "-Wl,--wrap=i2s_channel_write")
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=tud_msc_read10_cb" "-Wl,--wrap=tud_msc_write10_cb" "-Wl,--wrap=tud_msc_test_unit_ready_cb")

# Executing from flash (no XIP from PSRAM), the cores' initialised data lives in
# the PSRAM section .ext_ram.data: place it (ext_ram_data.ld) and pull in the
# constructor that fills it at boot (src/ext_ram_data.c); the root
# CMakeLists.txt stores its contents in flash after linking
if(NOT CONFIG_SPIRAM_XIP_FROM_PSRAM)
  target_linker_script(${COMPONENT_LIB} INTERFACE "${CMAKE_CURRENT_LIST_DIR}/ext_ram_data.ld")
  # and the game cores' code overlay (overlay.ld, src/overlay.c), after it
  set(tab5_overlay_script "${CMAKE_CURRENT_LIST_DIR}/overlay.ld")
  if(TAB5_SD_CORES)
    file(READ "${tab5_overlay_script}" tab5_overlay_text)
    string(REGEX REPLACE [=[(\. \+= )SIZEOF\(\.tab5_ovl\.[a-z0-9]+\);]=] [=[\1 48;]=] tab5_overlay_text "${tab5_overlay_text}")
    set(tab5_overlay_script "${CMAKE_BINARY_DIR}/sd_overlay.ld")
    file(WRITE "${tab5_overlay_script}" "${tab5_overlay_text}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/overlay.ld")
  endif()
  target_linker_script(${COMPONENT_LIB} INTERFACE "${tab5_overlay_script}")
  target_link_libraries(${COMPONENT_LIB} INTERFACE "-u tab5_ext_ram_data_init")
endif()
