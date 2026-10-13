#pragma once
#include <stdbool.h>
#include <stdint.h>
enum { TUSB_ROLE_HOST=1, TUSB_SPEED_FULL=2, HID_ITF_PROTOCOL_KEYBOARD=1,
       HID_PROTOCOL_BOOT=0, HID_PROTOCOL_REPORT=1 };
typedef struct { unsigned role, speed; } tusb_rhport_init_t;
bool tusb_init(uint8_t port, const tusb_rhport_init_t *init);
bool tuh_deinit(uint8_t port);
void tuh_task_ext(uint32_t timeout, bool in_isr);
uint8_t tuh_hid_interface_protocol(uint8_t address, uint8_t instance);
uint8_t tuh_hid_get_protocol(uint8_t address, uint8_t instance);
bool tuh_hid_set_protocol(uint8_t address, uint8_t instance, uint8_t protocol);
bool tuh_hid_receive_report(uint8_t address, uint8_t instance);
void tuh_mount_cb(uint8_t address);
void tuh_hid_mount_cb(uint8_t address,uint8_t instance,const uint8_t *desc,uint16_t length);
void tuh_hid_umount_cb(uint8_t address,uint8_t instance);
void tuh_hid_report_received_cb(uint8_t address,uint8_t instance,const uint8_t *report,uint16_t len);

bool tuh_task_event_ready(void);
