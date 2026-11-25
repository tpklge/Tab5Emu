if(CMAKE_BUILD_EARLY_EXPANSION)
  # IDF scans requirements of optional components too. Public/stable builds
  # must never require the private ROM or generated native assets.
  idf_component_register(REQUIRES esp_psram esp_timer)
  return()
endif()
if(NOT TAB5_PROFILE STREQUAL "sm64-native")
  idf_component_register()
  return()
endif()
set(SM64_NATIVE_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../../.tools/sm64-native-reference" CACHE PATH "Locally prepared pinned native SM64 checkout")
set(sm64_stage "${CMAKE_BINARY_DIR}/sm64-native-adapter")
execute_process(COMMAND ${PYTHON} "${CMAKE_CURRENT_LIST_DIR}/../../tools/stage_sm64_native.py" "${SM64_NATIVE_SOURCE}" "${sm64_stage}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Native SM64 needs locally prepared assets from the matching user ROM")
endif()
include("${sm64_stage}/sources.cmake")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../../tools/stage_sm64_native.py")
idf_component_register(SRCS src/port.c ${sm64_sources}
  INCLUDE_DIRS include PRIV_INCLUDE_DIRS compat ${sm64_includes}
  REQUIRES esp_psram esp_timer)
target_compile_definitions(${COMPONENT_LIB} PRIVATE VERSION_US ENABLE_SOFTRAST=1 CONVERT NON_MATCHING=1 AVOID_UB=1 TARGET_RS97=0 TARGET_FUNKEY=1 F3DEX_GBI_2E _LANGUAGE_C RETRO_GO=1 NO_SEGMENTED_MEMORY=1)
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -w -fno-strict-aliasing -msmall-data-limit=0 -fjump-tables -ftree-switch-conversion)
set_source_files_properties("${sm64_stage}/gfx_soft.c" "${SM64_NATIVE_SOURCE}/sm64-go/components/sm64-funkey/src/audio/synthesis.c" PROPERTIES COMPILE_OPTIONS "-O3")
add_custom_command(TARGET ${COMPONENT_LIB} POST_BUILD
  COMMAND ${PYTHON} "${CMAKE_CURRENT_LIST_DIR}/../../tools/bss_to_psram.py" ${CMAKE_OBJDUMP} ${CMAKE_OBJCOPY} $<TARGET_FILE:${COMPONENT_LIB}> --overlay=sm64 --overlay-rodata --overlay-data
  COMMENT "Moving native game code and private assets to the SD overlay" VERBATIM)
