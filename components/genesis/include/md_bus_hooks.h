#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// End of the contiguous cartridge ROM area that cannot overlap SRAM.
extern uint32_t tab5_md_fast_rom_end;
uint32_t tab5_md_read8(uint32_t address);
int tab5_md_write8(uint32_t address, uint32_t value);
int tab5_md_cart_read16(uint32_t address, uint32_t *value);
int tab5_md_cart_write16(uint32_t address, uint32_t value);
#ifdef __cplusplus
}
#endif
