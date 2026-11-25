#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
bool tab5_daedalus_init(uint8_t *ram, uint8_t *sp, uint32_t *vi, uint32_t *mi,
                        void (*poll)(void *), void *opaque);
void tab5_daedalus_close(void);
void tab5_daedalus_render_step(unsigned step);
// 0: not a graphics OSTask; 1: completed by Daedalus; -1: bounded failure.
int tab5_daedalus_task(void);
void tab5_daedalus_metrics(uint64_t *tasks, uint64_t *triangles,
                           uint64_t *pixels);
#ifdef __cplusplus
}
#endif
