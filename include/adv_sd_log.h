#pragma once
#include <stdint.h>
enum adv_sd_log_state { ADV_SD_STARTING, ADV_SD_ACTIVE, ADV_SD_ERROR };
struct adv_sd_log_status {
    enum adv_sd_log_state state;
    int error;
    uint32_t dropped, bytes, file;
};
/* Nonblocking status only. The logger never controls USB, input or BLE. */
void adv_sd_log_get_status(struct adv_sd_log_status *status);

#define ADV_LOG_CHUNK 128
struct adv_log_reply {
    uint32_t id, file, offset, size;
    int error;
    uint16_t length;
    uint8_t data[ADV_LOG_CHUNK];
};
enum adv_log_operation { ADV_LOG_NEXT, ADV_LOG_READ };
/* Thread context only; bounded wait. FS work stays on the SD owner thread. */
int adv_sd_log_fetch(enum adv_log_operation op, uint32_t file, uint32_t offset,
                     struct adv_log_reply *reply);
