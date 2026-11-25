/* SPDX-License-Identifier: 0BSD
 * Optional host-only comparison with the pinned mGBA library. Not firmware.
 * Runs guest frames and exports the guest's SRAM, without injecting results.
 */
#include <mgba/core/core.h>
#include <mgba/core/config.h>
#include <mgba-util/vfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>

int main(int argc, char **argv) {
  if (argc != 4) {
    fprintf(stderr, "usage: gba-reference ROM OUTPUT_SRAM FRAMES\n");
    return 2;
  }
  char *end;
  unsigned long frames = strtoul(argv[3], &end, 10);
  if (*end || !frames || frames > 1000000) return 2;
  struct mCore *core = mCoreFind(argv[1]);
  if (!core || !core->init(core)) return 3;
  mCoreInitConfig(core, NULL);
  mCoreConfigSetDefaultIntValue(&core->config, "skipBios", 1);
  mCoreConfigSetDefaultIntValue(&core->config, "useBios", 0);
  mCoreLoadConfig(core);
  mColor *video = calloc(240 * 160, sizeof(*video));
  if (!video) return 4;
  core->setVideoBuffer(core, video, 240);
  struct VFile *rom = VFileOpen(argv[1], O_RDONLY);
  if (!rom || !core->loadROM(core, rom)) return 5;
  core->reset(core);
  for (unsigned long i = 0; i < frames; ++i) core->runFrame(core);
  void *save = NULL;
  size_t size = core->savedataClone(core, &save);
  if (!save || !size) return 6;
  FILE *output = fopen(argv[2], "wbx");
  if (!output) return 7;
  if (fwrite(save, 1, size, output) != size || fclose(output)) return 8;
  printf("%lu guest frames, %zu SRAM bytes, signature %.4s\n", frames, size, (char *)save);
  free(save);
  mCoreConfigDeinit(&core->config);
  core->deinit(core);
  free(video);
  return 0;
}
