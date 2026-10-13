/* Private board port. Lifecycle calls: USB owner thread only. */
#pragma once
void usb_board_enter_host(void);
void usb_board_restore_debug(void);
/* IRQ-safe, only while the USB peripheral is clocked. No teardown here. */
void usb_board_quiet_irq(void);
