# Stage the pinned Musashi sources with the three ROM data fast paths bounded
# by the actual cartridge and SRAM mapping. No generated opcode/core rewrite.
set(tab5_md_cpu "${CMAKE_CURRENT_BINARY_DIR}/m68k-adapter")
file(MAKE_DIRECTORY "${tab5_md_cpu}")
file(GLOB tab5_md_cpu_files "${CMAKE_CURRENT_LIST_DIR}/../components/genesis/gwenesis/src/cpus/M68K/*.[ch]")
foreach(tab5_md_file ${tab5_md_cpu_files})
  get_filename_component(tab5_md_name "${tab5_md_file}" NAME)
  if(tab5_md_name STREQUAL "m68kcpu.h")
    file(READ "${tab5_md_file}" tab5_md_header)
    foreach(tab5_md_width 8 16 32)
      set(tab5_md_old "if (ADDRESS_68K(address) <  0x800000) return FETCH${tab5_md_width}ROM(ADDRESS_68K(address));")
      string(FIND "${tab5_md_header}" "${tab5_md_old}" tab5_md_found)
      if(tab5_md_found EQUAL -1)
        message(FATAL_ERROR "Pinned M68K fast path changed; audit SRAM adapter")
      endif()
      math(EXPR tab5_md_tail "${tab5_md_width} / 8 - 1")
      string(REPLACE "${tab5_md_old}"
        "if (ADDRESS_68K(address) + ${tab5_md_tail} < tab5_md_fast_rom_end) return FETCH${tab5_md_width}ROM(ADDRESS_68K(address));"
        tab5_md_header "${tab5_md_header}")
    endforeach()
    # The 64 KiB work RAM is mirrored throughout the top 2 MiB of the bus.
    string(REPLACE ">= 0xFF0000" ">= 0xE00000" tab5_md_header "${tab5_md_header}")
    string(PREPEND tab5_md_header "#include \"md_bus_hooks.h\"\n")
    file(CONFIGURE OUTPUT "${tab5_md_cpu}/${tab5_md_name}" CONTENT "${tab5_md_header}" @ONLY)
  elseif(tab5_md_name STREQUAL "m68k.h")
    file(READ "${tab5_md_file}" tab5_md_header)
    foreach(tab5_md_macro FETCH32ROM FETCH32RAM WRITE32RAM)
      string(REGEX MATCH "#define ${tab5_md_macro}[^\n]*" tab5_md_definition "${tab5_md_header}")
      if(NOT tab5_md_definition)
        message(FATAL_ERROR "Pinned M68K RAM macro changed; audit word-boundary adapter")
      endif()
    endforeach()
    string(REPLACE
      "#define FETCH32ROM(A) ( (*(unsigned int *)&ROM_DATA[(A)] << 16) | (*(unsigned int *)&ROM_DATA[(A)] >> 16) )"
      "#define FETCH32ROM(A) (((unsigned int)FETCH16ROM(A) << 16) | FETCH16ROM((A) + 2))"
      tab5_md_header "${tab5_md_header}")
    string(REPLACE
      "#define FETCH32RAM(A) ( (*(unsigned int *)&M68K_RAM[(A&0XFFFF)] << 16) | (*(unsigned int *)&M68K_RAM[(A&0XFFFF)] >> 16) )"
      "#define FETCH32RAM(A) (((unsigned int)FETCH16RAM(A) << 16) | FETCH16RAM((A) + 2))"
      tab5_md_header "${tab5_md_header}")
    string(REPLACE
      "#define WRITE32RAM(A, V) ((*(unsigned int *)&M68K_RAM[(A)&0XFFFF] =( ((V) << 16) | ((V) >> 16) ) ))"
      "#define WRITE32RAM(A, V) do { WRITE16RAM((A), (V) >> 16); WRITE16RAM((A) + 2, (V)); } while (0)"
      tab5_md_header "${tab5_md_header}")
    file(CONFIGURE OUTPUT "${tab5_md_cpu}/${tab5_md_name}" CONTENT "${tab5_md_header}" @ONLY)
  else()
    configure_file("${tab5_md_file}" "${tab5_md_cpu}/${tab5_md_name}" COPYONLY)
  endif()
endforeach()
