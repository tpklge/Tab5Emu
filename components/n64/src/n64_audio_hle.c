// SPDX-License-Identifier: GPL-2.0-or-later
// Bounded ABI1 frontend for the pinned Mupen64Plus HLE DSP. No plugin loader.
#include "n64_audio_hle.h"
#include "hle_internal.h"
#include <setjmp.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
static struct hle_t *audio;
static jmp_buf invalid;
static void (*poll_callback)(void *);
static void *poll_context;
static unsigned command_count;
void tab5_n64_audio_abort(void) { longjmp(invalid, 1); }
void tab5_n64_audio_span(uint32_t offset, size_t bytes, bool ram) {
  const uint32_t size = ram ? 8 * 1024 * 1024 : 4096;
  if (offset > size || bytes > size - offset)
    tab5_n64_audio_abort();
}
void *tab5_n64_audio_pointer(const void *base, uint32_t offset, size_t bytes) {
  if (base == audio->dram)
    tab5_n64_audio_span(offset, bytes, true);
  if (base == audio->dmem || base == audio->imem || base == audio->alist_buffer)
    tab5_n64_audio_span(offset, bytes, false);
  return (void *)((const uint8_t *)base + offset);
}
void tab5_n64_audio_step(void) {
  if (++command_count > 8192)
    tab5_n64_audio_abort();
  if (!(command_count & 63) && poll_callback)
    poll_callback(poll_context);
}
void HleVerboseMessage(void *c, const char *f, ...) {
  (void)c;
  (void)f;
}
void HleInfoMessage(void *c, const char *f, ...) {
  (void)c;
  (void)f;
}
void HleWarnMessage(void *c, const char *f, ...) {
  (void)c;
  (void)f;
  tab5_n64_audio_abort();
}
void HleErrorMessage(void *c, const char *f, ...) {
  (void)c;
  (void)f;
  tab5_n64_audio_abort();
}
// Completion/interrupts are handled by the console's SP dispatcher.
void rsp_break(struct hle_t *h, unsigned int bits) {
  (void)h;
  (void)bits;
}
bool tab5_n64_audio_hle_init(uint8_t *ram, uint8_t *sp, void (*poll)(void *),
                             void *ctx) {
  if (audio)
    return false;
  audio = calloc(1, sizeof(*audio));
  if (!audio)
    return false;
  audio->dram = ram;
  audio->dmem = sp;
  audio->imem = sp + 4096;
  poll_callback = poll;
  poll_context = ctx;
  return true;
}
void tab5_n64_audio_hle_close(void) {
  free(audio);
  audio = NULL;
}
int tab5_n64_audio_hle_task(void) {
  if (!audio)
    return 0;
  uint32_t *t = (uint32_t *)(audio->dmem + 0xfc0);
  if (t[0] != 2)
    return 0;
  uint32_t data = t[6] & 0xffffff;
  if (data > 8 * 1024 * 1024 - 0x34 || (data & 3))
    return 0;
  uint32_t *p = (uint32_t *)(audio->dram + data);
  if (p[0] != 1 || p[0x30 / 4] != 0xf0000f00)
    return 0;
  void (*volatile process)(struct hle_t *) = NULL;
  switch (p[0x28 / 4]) {
  case 0x1e24138c:
    process = alist_process_audio;
    break;
  case 0x1dc8138c:
    process = alist_process_audio_ge;
    break;
  case 0x1e3c1390:
    process = alist_process_audio_bc;
    break;
  default:
    return 0;
  }
  if ((t[12] & 3) || (t[13] & 7) || t[13] > 65536)
    return -1;
  if (setjmp(invalid))
    return -1;
  tab5_n64_audio_span(t[12] & 0xffffff, t[13], true);
  command_count = 0;
  process(audio);
  return 1;
}
