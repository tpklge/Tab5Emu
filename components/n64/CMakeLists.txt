set(stage "${CMAKE_BINARY_DIR}/n64-adapter")
execute_process(COMMAND ${PYTHON} "${CMAKE_CURRENT_LIST_DIR}/../../tools/stage_n64.py" "${stage}"
  RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "N64 source staging failed")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_LIST_DIR}/../../tools/stage_n64.py")
set(tab5_hle_sources "")
set(tab5_hle_includes "")
if(TAB5_N64_DAEDALUS)
  set(daedalus_root "${CMAKE_CURRENT_LIST_DIR}/../daedalus")
  set(daedalus_stage "${CMAKE_BINARY_DIR}/daedalus-adapter")
  execute_process(COMMAND ${PYTHON} "${CMAKE_CURRENT_LIST_DIR}/../../tools/stage_daedalus.py" "${daedalus_stage}"
    RESULT_VARIABLE daedalus_result)
  if(NOT daedalus_result EQUAL 0)
    message(FATAL_ERROR "Daedalus source staging failed")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/../../tools/stage_daedalus.py")
  set(tab5_hle_sources "${daedalus_root}/src/software.cpp")
  foreach(file DLParser BaseRenderer Microcode RDPStateManager RDP TextureInfo)
    list(APPEND tab5_hle_sources "${daedalus_stage}/HLEGraphics/${file}.cpp")
  endforeach()
  list(APPEND tab5_hle_sources "${daedalus_stage}/HLEGraphics/uCodes/Ucode.cpp" "${daedalus_stage}/Graphics/ColourValue.cpp")
  set(tab5_hle_includes "${daedalus_root}/include" "${daedalus_root}/compat" "${daedalus_stage}"
    "${CMAKE_CURRENT_LIST_DIR}/../../experiments/daedalus/glm")
endif()
set(tab5_audio_sources "")
set(tab5_audio_includes "")
if(TAB5_N64_AUDIO_HLE)
  set(audio_stage "${CMAKE_BINARY_DIR}/n64-audio-adapter")
  execute_process(COMMAND ${PYTHON} "${CMAKE_CURRENT_LIST_DIR}/../../tools/stage_n64_audio.py" "${audio_stage}" RESULT_VARIABLE audio_result)
  if(NOT audio_result EQUAL 0)
    message(FATAL_ERROR "N64 audio DSP staging failed")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../../tools/stage_n64_audio.py")
  set(tab5_audio_sources src/n64_audio_hle.c "${audio_stage}/alist.c" "${audio_stage}/alist_audio.c" "${audio_stage}/audio.c" "${audio_stage}/memory.c")
  set_source_files_properties(src/n64_audio_hle.c PROPERTIES INCLUDE_DIRECTORIES "${audio_stage}")
endif()
idf_component_register(
  SRCS src/n64_console.c src/n64_plugins.c ${tab5_hle_sources} ${tab5_audio_sources}
    "../../experiments/n64/mupen/src/device/pif/cic.c"
    "../../experiments/n64/mupen/src/device/pif/n64_cic_nus_6105.c"
    "${stage}/device/r4300/pure_interp.c" "${stage}/device/r4300/cp1.c" "${stage}/device/r4300/cp2.c"
    "${stage}/rsp/su.c" "${stage}/rsp/vu/vu.c" "${stage}/rsp/vu/multiply.c"
    "${stage}/rsp/vu/add.c" "${stage}/rsp/vu/select.c" "${stage}/rsp/vu/logical.c" "${stage}/rsp/vu/divide.c"
    "${stage}/rdp/n64video.c"
  INCLUDE_DIRS include
  PRIV_INCLUDE_DIRS ${tab5_hle_includes} "${stage}" "${stage}/device/r4300" "${stage}/rsp" "${stage}/rdp"
    "../../experiments/n64/mupen/src" "../../experiments/n64/mupen/src/device/r4300"
    "../../experiments/n64/mupen/subprojects/xxhash"
  REQUIRES esp_psram)
if(TAB5_N64_DAEDALUS)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE TAB5_N64_DAEDALUS DAEDALUS_ENDIAN_LITTLE=1 DAEDALUS_SOFTWARE)
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE NO_ASM USE_CLIENT_ENDIAN PLUGIN_API_VERSION=0x0101)
target_compile_options(${COMPONENT_LIB} PRIVATE -O3 -fjump-tables -ftree-switch-conversion -fwrapv -fno-strict-aliasing -msmall-data-limit=0
  -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable -Wno-misleading-indentation
  -Wno-unused-but-set-parameter
  -Wno-parentheses -Wno-implicit-fallthrough -Wno-sign-compare -Wno-error=maybe-uninitialized)
set(tab5_n64_overlay_args "")
if(TAB5_SD_CORES)
  list(APPEND tab5_n64_overlay_args --overlay=n64 --overlay-rodata)
endif()
add_custom_command(TARGET ${COMPONENT_LIB} POST_BUILD
  COMMAND ${PYTHON} ${CMAKE_CURRENT_LIST_DIR}/../../tools/bss_to_psram.py
    ${CMAKE_OBJDUMP} ${CMAKE_OBJCOPY} $<TARGET_FILE:${COMPONENT_LIB}> ${tab5_n64_overlay_args}
    --keep SR,VR,VACC,CR,VCO,VCC,VCE
  COMMENT "Moving N64 renderer state to PSRAM" VERBATIM)
