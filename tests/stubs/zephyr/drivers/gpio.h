#pragma once
struct gpio_dt_spec { int unused; };
#define GPIO_DT_SPEC_GET(...) {0}
int gpio_pin_get_dt(const struct gpio_dt_spec *);
