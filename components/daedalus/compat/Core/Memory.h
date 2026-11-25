#pragma once
#include "Base/Types.h"
#include "Ultra/ultra_rcp.h"
constexpr u32 MAX_RAM_ADDRESS=8*1024*1024;
constexpr unsigned U8_TWIDDLE=3,U16_TWIDDLE=1;
extern u8 *g_pu8RamBase,*g_pu8SpMemBase;
#define g_ps8RamBase ((s8*)g_pu8RamBase)
u32 Memory_VI_GetRegister(u32);void Memory_MI_SetRegisterBits(u32,u32);
