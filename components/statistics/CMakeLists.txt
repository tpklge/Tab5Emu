idf_component_register(
  INCLUDE_DIRS "include"
  SRCS "src/statistics.cpp" "src/frame_metrics.cpp"
  REQUIRES "format" "esp_timer")
target_compile_options(${COMPONENT_LIB} PRIVATE -fstack-usage -Wframe-larger-than=2048)
