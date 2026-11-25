# Game Boy / Color: gnuboy, from the esp-cpp/gnuboy submodule (the core in
# gnuboy/gnuboy, its glue in gnuboy/port)
file(GLOB gbc_core_sources "gnuboy/gnuboy/src/*.c" "gnuboy/port/*.cpp" "src/*.cpp" "src/*.c")
list(FILTER gbc_core_sources EXCLUDE REGEX "/gnuboy/port/(gameboy|gbc_shared_memory)[.]cpp$|/gnuboy/gnuboy/src/(rtc|mem|sound|loader)[.]c$")
set(gb_loader_source "${CMAKE_CURRENT_LIST_DIR}/gnuboy/gnuboy/src/loader.c")
set(gb_loader_adapter "${CMAKE_CURRENT_BINARY_DIR}/gb_loader.c")
execute_process(COMMAND ${PYTHON} "${CMAKE_CURRENT_LIST_DIR}/../../tools/stage_gb_loader.py"
  "${gb_loader_source}" "${gb_loader_adapter}" COMMAND_ERROR_IS_FATAL ANY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${gb_loader_source}" "${CMAKE_CURRENT_LIST_DIR}/../../tools/stage_gb_loader.py")
list(APPEND gbc_core_sources "${gb_loader_adapter}")
idf_component_register(
  INCLUDE_DIRS "gnuboy/port/include" "include"
  SRCS ${gbc_core_sources}
  PRIV_INCLUDE_DIRS "gnuboy/gnuboy/include"
  REQUIRES "tab5-emu" "statistics" "shared_memory"
  )

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-misleading-indentation -Wno-implicit-fallthrough -Wno-unused-function -Wno-unused-variable $<$<COMPILE_LANGUAGE:C>:-Wno-discarded-qualifiers>)
target_compile_definitions(${COMPONENT_LIB} PRIVATE GNUBOY_NO_MINIZIP GNUBOY_NO_SCREENSHOT IS_LITTLE_ENDIAN)
# the core's static state (.bss and .data) lives in PSRAM (see the post-build step; the P4
# linker script has no ldgen placeholder for external RAM); no gp-relative
# small data then
target_compile_options(${COMPONENT_LIB} PRIVATE -msmall-data-limit=0)
# executing from flash, the core's code is part of the PSRAM code overlay
# (components/tab5-emu/overlay.ld)
include(${CMAKE_CURRENT_LIST_DIR}/../../tools/overlay.cmake)
tab5_overlay_args(gbc overlay_args)
add_custom_command(TARGET ${COMPONENT_LIB} POST_BUILD
  COMMAND ${PYTHON} ${CMAKE_CURRENT_LIST_DIR}/../../tools/bss_to_psram.py
          ${CMAKE_OBJDUMP} ${CMAKE_OBJCOPY} $<TARGET_FILE:${COMPONENT_LIB}> ${overlay_args}
          # .data to PSRAM: via .rodata where XIP copies it into PSRAM (writable),
          # else into .ext_ram.data (filled at boot from flash; see the script)
          --data --data-into=$<IF:$<BOOL:${CONFIG_SPIRAM_XIP_FROM_PSRAM}>,rodata,ext_ram>
  COMMENT "Moving the gbc core's .bss and .data to PSRAM"
  VERBATIM)
# globals other cores in this firmware define too (the box doesn't build
# those): renamed in this library, its glue included
add_custom_command(TARGET ${COMPONENT_LIB} POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} --redefine-syms=${CMAKE_CURRENT_LIST_DIR}/symbol_renames.syms $<TARGET_FILE:${COMPONENT_LIB}>
  COMMENT "Renaming the globals another core also defines"
  VERBATIM)

# Jump tables for the core's switch statements (the interpreter's opcode
# dispatch). ESP-IDF turns them off for everything, because a table in flash
# breaks code that must run while the cache is disabled; this core runs from
# its PSRAM overlay with the cache on, and its tables move into the overlay
# with its other read-only data. Measured on the Tab5: 12% less CPU per frame.
target_compile_options(${COMPONENT_LIB} PRIVATE -fjump-tables -ftree-switch-conversion)
