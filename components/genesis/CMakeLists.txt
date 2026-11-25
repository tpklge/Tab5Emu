include(${CMAKE_CURRENT_LIST_DIR}/../../tools/md_cpu_adapter.cmake)
file(GLOB genesis_sources
    "src/*.c" "src/*.cpp" "gwenesis/port/*.c" "gwenesis/port/*.cpp"
    "gwenesis/src/cpus/Z80/*.c" "gwenesis/src/sound/*.c"
    "gwenesis/src/vdp/*.c" "gwenesis/src/io/*.c" "gwenesis/src/savestate/*.c")
list(FILTER genesis_sources EXCLUDE REGEX "/gwenesis/port/genesis[.]cpp$|/gwenesis/src/io/gwenesis_io[.]c$")
list(APPEND genesis_sources "${tab5_md_cpu}/m68kcpu.c")
idf_component_register(
    SRCS ${genesis_sources}
    INCLUDE_DIRS
        "include"
        "gwenesis/port/include"
        "gwenesis/src"
        "gwenesis/src/bus"
        "${tab5_md_cpu}"
        "gwenesis/src/cpus/Z80"
        "gwenesis/src/sound"
        "gwenesis/src/vdp"
        "gwenesis/src/io"
        "gwenesis/src/savestate"
    REQUIRES
        shared_memory
        tab5-emu
        statistics
)
# target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-char-subscripts -Wno-attributes -Wno-implicit-fallthrough -Wno-unused-function -Wno-unused-variable -Wno-discarded-qualifiers)
# the core's static state lives in PSRAM (see the post-build step); no
# gp-relative small data then
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-const-variable -Wno-unused-value -Ofast -msmall-data-limit=0 -fjump-tables -ftree-switch-conversion
  # gcc 15 (ESP-IDF 6.x) promotes these to errors; the core is upstream code
  -Wno-error=sizeof-pointer-memaccess -Wno-char-subscripts -Wno-attributes -Wno-implicit-fallthrough
  -Wno-unused-function -Wno-unused-variable -Wno-error=discarded-qualifiers -Wno-error=incompatible-pointer-types
  -Wno-error=int-conversion -Wno-error=implicit-function-declaration -Wno-error=stringop-overflow
  -Wno-error=array-bounds -Wno-error=maybe-uninitialized -Wno-error=address -Wno-error=misleading-indentation)
# target_compile_definitions(${COMPONENT_LIB} PRIVATE GWENESIS_AUDIO_ACCURATE=0 BUILD_TABLES=0 TABLES_FULL=0)
target_compile_definitions(${COMPONENT_LIB} PRIVATE GWENESIS_AUDIO_ACCURATE=0)
# executing from flash, the core's code is part of the PSRAM code overlay
# (components/tab5-emu/overlay.ld)
include(${CMAKE_CURRENT_LIST_DIR}/../../tools/overlay.cmake)
tab5_overlay_args(genesis overlay_args)
add_custom_command(TARGET ${COMPONENT_LIB} POST_BUILD
  COMMAND ${PYTHON} ${CMAKE_CURRENT_LIST_DIR}/../../tools/bss_to_psram.py
          ${CMAKE_OBJDUMP} ${CMAKE_OBJCOPY} $<TARGET_FILE:${COMPONENT_LIB}> ${overlay_args}
  COMMENT "Moving the Genesis core's .bss to PSRAM"
  VERBATIM)
