#pragma once
#include <zephyr/device.h>
int display_blanking_on(const struct device *);
int display_blanking_off(const struct device *);
