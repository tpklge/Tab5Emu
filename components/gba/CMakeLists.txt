# GBA: gpSP (interpreter build; no dynarec on RISC-V), driven through its
# own libretro front end by a minimal libretro host in src/gba.cpp
set(GPSP gpsp)
set(GPSP_COMMON ${GPSP}/libretro/libretro-common)
option(TAB5_GBA_HEAP_CHECK "Check heap integrity around GBA lifecycle and every frame (diagnostic only)" OFF)
set(gpsp_extra_sources "")
set(gpsp_memmap ${GPSP}/memmap.c)
if(TAB5_GBA_RV32)
  set(gpsp_extra_sources rv32/cpu_threaded.c rv32/riscv/riscv_stub.S rv32/platform.c)
  set(gpsp_memmap "")
endif()
idf_component_register(
  SRCS
    ${GPSP}/main.c ${GPSP}/gba_memory.c ${GPSP}/savestate.c ${GPSP}/input.c ${GPSP}/sound.c
    ${GPSP}/cheats.c ${gpsp_memmap} ${GPSP}/serial.c ${GPSP}/gbp.c ${GPSP}/rfu.c
    ${GPSP}/serial_proto.c ${GPSP}/libretro/libretro.c ${GPSP}/gba_cc_lut.c
    ${GPSP}/video.cc ${GPSP}/cpu.cc ${GPSP}/open_gba_bios.c
    ${GPSP_COMMON}/compat/compat_posix_string.c ${GPSP_COMMON}/compat/compat_strl.c
    ${GPSP_COMMON}/compat/fopen_utf8.c ${GPSP_COMMON}/encodings/encoding_utf.c
    ${GPSP_COMMON}/file/file_path.c ${GPSP_COMMON}/file/file_path_io.c
    ${GPSP_COMMON}/streams/file_stream.c ${GPSP_COMMON}/string/stdstring.c
    ${GPSP_COMMON}/time/rtime.c ${GPSP_COMMON}/vfs/vfs_implementation.c
    "src/gba.cpp" ${gpsp_extra_sources}
  INCLUDE_DIRS "include"
  PRIV_INCLUDE_DIRS "${GPSP}" "${GPSP}/libretro" "${GPSP_COMMON}/include" "src" "rv32"
  REQUIRES tab5-emu statistics shared_memory settings
  LDFRAGMENTS "linker.lf"
)
if(TAB5_GBA_RV32)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE HAVE_DYNAREC RISCV_ARCH MMAP_JIT_CACHE SMALL_TRANSLATION_CACHE TAB5_GBA_RV32)
endif()
if(TAB5_GBA_HEAP_CHECK)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE TAB5_GBA_HEAP_CHECK)
endif()
# ROM cache: 1 MB blocks in PSRAM, enough for a 16 MB ROM (larger ones page
# in from the SD card, which stalls the emulation)
target_compile_definitions(${COMPONENT_LIB} PRIVATE __LIBRETRO__ ROM_BUFFER_SIZE=16)
# the core's static state (backup RAM, VRAM, ...) lives in PSRAM (see the
# post-build step; the CPU registers, IO registers, OAM and palette stay
# internal: ~5 KB the interpreter touches constantly); no gp-relative small
# data then
target_compile_options(${COMPONENT_LIB} PRIVATE -O3 -fno-strict-aliasing -fomit-frame-pointer -momit-leaf-frame-pointer -fjump-tables -ftree-switch-conversion -msmall-data-limit=0 -include ${CMAKE_CURRENT_LIST_DIR}/gpsp/retro_inline.h
  -Wno-error -w -Wno-error=incompatible-pointer-types -Wno-error=int-conversion
  -Wno-error=implicit-function-declaration -Wno-error=discarded-qualifiers)
# executing from flash, the core's code is part of the PSRAM code overlay
# (components/tab5-emu/overlay.ld)
include(${CMAKE_CURRENT_LIST_DIR}/../../tools/overlay.cmake)
tab5_overlay_args(gba overlay_args)
set(tab5_gba_internal "reg,reg_mode,spsr,cpu_ticks,io_registers,oam_ram,palette_ram,palette_ram_converted,oam_update_count")
if(TAB5_GBA_RV32)
  # The assembly defines the contiguous file in one .bss.regfile section.
  # Keeping the individual symbol names does not keep that section internal.
  string(APPEND tab5_gba_internal ",regfile")
endif()
add_custom_command(TARGET ${COMPONENT_LIB} POST_BUILD
  COMMAND ${PYTHON} ${CMAKE_CURRENT_LIST_DIR}/../../tools/bss_to_psram.py
          ${CMAKE_OBJDUMP} ${CMAKE_OBJCOPY} $<TARGET_FILE:${COMPONENT_LIB}> ${overlay_args}
          --keep ${tab5_gba_internal}
  COMMENT "Moving the GBA core's .bss to PSRAM"
  VERBATIM)
# the interpreter loop in internal RAM (see linker.lf and main/Kconfig.projbuild)
if(CONFIG_TAB5_EMU_GBA_CPU_IRAM)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE GBA_CPU_IN_IRAM)
endif()

target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=execute_arm" "-Wl,--wrap=update_gba" "-Wl,--wrap=update_scanline" "-Wl,--wrap=load_gamepak_page" "-Wl,--wrap=filestream_read")
if(TAB5_GBA_RV32)
  target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=execute_arm_translate")
endif()
