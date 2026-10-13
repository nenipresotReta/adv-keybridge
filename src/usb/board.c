/* SPDX-License-Identifier: MIT */
#include <zephyr/init.h>
#include <esp_rom_gpio.h>
#include <soc/gpio_sig_map.h>
#include <soc/gpio_pins.h>
#include <soc/rtc_cntl_struct.h>
#include <hal/usb_wrap_ll.h>
#include "tusb.h"
#include "portable/synopsys/dwc2/dwc2_type.h"
#include "board.h"

void usb_board_enter_host(void) {
    usb_wrap_ll_enable_bus_clock(true);
    usb_wrap_ll_reset_register();
    usb_wrap_ll_phy_enable_external(&USB_WRAP, false);
    usb_wrap_ll_phy_enable_pad(&USB_WRAP, true);
    usb_wrap_ll_phy_enable_pin_exchg(&USB_WRAP, false);
    usb_wrap_ll_phy_disable_vref_override(&USB_WRAP);
    usb_wrap_ll_phy_disable_srp_sessend_override(&USB_WRAP);
    const usb_wrap_pull_override_vals_t pulls = {.dp_pd=true, .dm_pd=true};
    usb_wrap_ll_phy_enable_pull_override(&USB_WRAP, &pulls);
    esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ZERO_INPUT, USB_OTG_IDDIG_IN_IDX, false);
    esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ZERO_INPUT, USB_SRP_BVALID_IN_IDX, false);
    esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ONE_INPUT, USB_OTG_VBUSVALID_IN_IDX, false);
    esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ONE_INPUT, USB_OTG_AVALID_IN_IDX, false);
}

void usb_board_restore_debug(void) {
    usb_wrap_ll_phy_disable_pull_override(&USB_WRAP);
    RTCCNTL.usb_conf.sw_hw_usb_phy_sel = 1;
    RTCCNTL.usb_conf.sw_usb_phy_sel = 0;
}

void usb_board_quiet_irq(void) {
    dwc2_regs_t *regs = (dwc2_regs_t *)0x60080000u;
    regs->gintmsk = 0;
    regs->gahbcfg &= ~GAHBCFG_GINT;
}

static int restore_at_boot(void) {
    /* RTC mux survives some resets. Restore Serial/JTAG before its driver. */
    RTCCNTL.usb_conf.sw_hw_usb_phy_sel = 1;
    RTCCNTL.usb_conf.sw_usb_phy_sel = 0;
    return 0;
}
SYS_INIT(restore_at_boot, PRE_KERNEL_1, 1);
