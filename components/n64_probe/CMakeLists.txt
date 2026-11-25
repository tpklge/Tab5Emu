set(probe "${CMAKE_CURRENT_LIST_DIR}/../../experiments/n64")
idf_component_register(
 SRCS "${probe}/probe/probe.c" "${probe}/probe/pure_interp_probe.c"
      "${probe}/mupen/src/device/r4300/cp1.c" "${probe}/mupen/src/device/r4300/cp2.c"
 INCLUDE_DIRS "${probe}/probe" "${probe}/mupen/src" "${probe}/mupen/src/device/r4300" "${probe}/mupen/subprojects/xxhash")
target_compile_options(${COMPONENT_LIB} PRIVATE -O3 -Wno-unused-parameter -Wno-unused-function)
