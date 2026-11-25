// Restore the USB-C console before every software restart, including panic.
// The PHY mux is in LP_SYS and survives the reset. Leaving it swapped parks
// the factory launcher's Serial/JTAG interface on the unconnected I2S pins.
// This path must work with flash/cache unavailable; no heap, RTOS or logs.
#include "esp_attr.h"
#include "hal/usb_serial_jtag_ll.h"
#include "riscv/rv_utils.h"
#include "soc/usb_wrap_struct.h"

void __attribute__((noreturn)) __real_esp_restart_noos(void);
void IRAM_ATTR __attribute__((noreturn)) __wrap_esp_restart_noos(void) {
  rv_utils_intr_global_disable();
  LP_SYS.usb_ctrl.sw_usb_phy_sel = 0;
  LP_SYS.usb_ctrl.sw_hw_usb_phy_sel = 0;
  usb_serial_jtag_ll_phy_enable_pad(true);
  __asm__ volatile("fence iorw, iorw" ::: "memory");
  __real_esp_restart_noos();
}
