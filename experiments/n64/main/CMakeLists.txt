idf_component_register(
 SRCS "main.cpp" "../probe/probe.c" "../probe/pure_interp_probe.c"
      "../mupen/src/device/r4300/cp1.c" "../mupen/src/device/r4300/cp2.c"
      "../../../components/tab5-emu/src/paludo_platform.cpp"
      "../../../components/tab5-emu/src/usb_restart.c"
 INCLUDE_DIRS "../probe" "../mupen/src" "../mupen/src/device/r4300"
              "../mupen/subprojects/xxhash" "../../../components/tab5-emu/include"
 REQUIRES app_update esp_timer esp_driver_usb_serial_jtag esp_psram mbedtls)
target_compile_options(${COMPONENT_LIB} PRIVATE -O3 -Wno-unused-parameter -Wno-unused-function)
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_restart_noos")
