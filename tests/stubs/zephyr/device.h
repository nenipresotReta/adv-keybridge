#pragma once
#include <stdbool.h>
struct device { unsigned unused; };
extern const struct device test_lcd;
#define DEVICE_DT_GET(...) (&test_lcd)
bool device_is_ready(const struct device *);
