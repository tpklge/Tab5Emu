#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
bool tab5_n64_audio_hle_init(uint8_t *ram, uint8_t *sp, void (*poll)(void *),
                             void *ctx);
void tab5_n64_audio_hle_close(void);
// 0: unknown/non-audio (use CXD4); 1: completed; -1: invalid recognized task.
int tab5_n64_audio_hle_task(void);
void tab5_n64_audio_abort(void);
void *tab5_n64_audio_pointer(const void *base, uint32_t offset, size_t bytes);
void tab5_n64_audio_span(uint32_t offset, size_t bytes, bool ram);
void tab5_n64_audio_step(void);
#ifdef __cplusplus
}
#endif
