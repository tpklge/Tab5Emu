#pragma once
#include <stddef.h>
#include <stdint.h>
struct n64_probe_result {
 int passed;
 size_t core_bytes,instructions;
 uint32_t cause,epc;
 uint64_t regs[32];
 const char *unsupported;
};
#ifdef __cplusplus
extern "C" {
#endif
int n64_probe_run(const uint8_t *program,size_t size,struct n64_probe_result *out);

#ifdef __cplusplus
}
#endif
