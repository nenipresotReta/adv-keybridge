#pragma once
#define GPIO_NUM_0 0
#define ESP_SLEEP_WAKEUP_EXT0 2
int esp_sleep_enable_ext0_wakeup(int,int);
int esp_sleep_disable_wakeup_source(int);
int esp_sleep_get_wakeup_cause(void);
