#pragma once
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define MEM_SLOW 0
#define MEM_FAST 1
#define RG_PATH_SAVE_SRAM 0
#define RG_PATH_SAVE_STATE 1
void *rg_alloc(size_t bytes,int kind);
int64_t rg_system_timer(void);
void rg_storage_commit(void);
char *rg_emu_get_path(int type,const char *name);
void tab5_sm64_panic(const char *format,...);
#define RG_LOGI(...) do {printf("sm64: ");printf(__VA_ARGS__);printf("\n");} while(0)
#define RG_LOGW(...) RG_LOGI(__VA_ARGS__)
#define RG_LOGE(...) RG_LOGI(__VA_ARGS__)
#define RG_LOGD(...) ((void)0)
#define RG_PANIC(...) tab5_sm64_panic(__VA_ARGS__)
