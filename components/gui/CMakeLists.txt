idf_component_register(
  INCLUDE_DIRS "include"
  SRCS "src/gui.cpp" "src/library_tools.cpp" "src/library_pages.cpp" "src/controls_editor.cpp" "src/cover_worker.cpp"
  REQUIRES lvgl emu-ui timer logger rom_info settings tab5-emu app_update esp_app_format esp_driver_jpeg)
target_compile_options(${COMPONENT_LIB} PRIVATE -fstack-usage -Wframe-larger-than=2048)

# Preserve normal optimization in the index/cover workers and editor. Size
# only the original frontend and its toolbar; emulator cores are unchanged.
if(NOT TAB5_GBA_RV32)
  set_source_files_properties("src/gui.cpp" "src/library_tools.cpp" PROPERTIES COMPILE_OPTIONS "-Os")
endif()
