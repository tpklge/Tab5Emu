# SNES: snes9x2005 (libretro fork of Snes9x 1.43 / CATSFC), driven without
# libretro by src/snes_glue.c + src/snes.cpp
set(SNES_CORE snes9x2005/source)
file(GLOB SNES_CORE_SRCS ${CMAKE_CURRENT_LIST_DIR}/${SNES_CORE}/*.c)
# files that are #included by others, and the non-blargg APU
list(FILTER SNES_CORE_SRCS EXCLUDE REGEX "(dsp1emu|dsp2emu|dsp4emu|apu|soundux|spc700)\\.c$")
list(FILTER SNES_CORE_SRCS EXCLUDE REGEX "/apu_blargg[.]c$")
set(tab5_snes_apu "${CMAKE_CURRENT_BINARY_DIR}/snes-apu/snes_apu.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_LIST_DIR}/../../tools/stage_snes_apu.py)
execute_process(COMMAND ${PYTHON} ${CMAKE_CURRENT_LIST_DIR}/../../tools/stage_snes_apu.py
  ${CMAKE_CURRENT_LIST_DIR}/${SNES_CORE}/apu_blargg.c ${tab5_snes_apu}
  COMMAND_ERROR_IS_FATAL ANY)
idf_component_register(
  SRCS ${SNES_CORE_SRCS} ${tab5_snes_apu} "src/snes_glue.c" "src/snes.cpp"
  INCLUDE_DIRS "include"
  PRIV_INCLUDE_DIRS "${SNES_CORE}" "snes9x2005/libretro-common/include" "src"
  REQUIRES tab5-emu statistics shared_memory
)
# Keep the large PPU/CPU build at O2; optimize the SPC interpreter/mixer loop.
set_source_files_properties(${tab5_snes_apu} PROPERTIES COMPILE_OPTIONS "-O3")
target_compile_definitions(${COMPONENT_LIB} PRIVATE USE_BLARGG_APU LOAD_FROM_MEMORY LAGFIX __LIBRETRO__ SNES_HOST_ROM_ALLOC)
# the core's ~700 KB of static state lives in PSRAM (its .bss is renamed to the
# PSRAM section after the archive is built); no gp-relative small data then
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -fwrapv -fno-strict-aliasing -fomit-frame-pointer -momit-leaf-frame-pointer -fjump-tables -ftree-switch-conversion -msmall-data-limit=0
  -Wno-error -w -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -Wno-error=implicit-function-declaration -Wno-error=discarded-qualifiers)
# executing from flash, the core's code is part of the PSRAM code overlay
# (components/tab5-emu/overlay.ld)
include(${CMAKE_CURRENT_LIST_DIR}/../../tools/overlay.cmake)
tab5_overlay_args(snes overlay_args)
add_custom_command(TARGET ${COMPONENT_LIB} POST_BUILD
  COMMAND ${PYTHON} ${CMAKE_CURRENT_LIST_DIR}/../../tools/bss_to_psram.py
          ${CMAKE_OBJDUMP} ${CMAKE_OBJCOPY} $<TARGET_FILE:${COMPONENT_LIB}> ${overlay_args}
          --keep CPU,ICPU,PPU,IPPU,Settings
  COMMENT "Moving the SNES core's .bss to PSRAM (CPU / PPU state stays internal)"
  VERBATIM)
