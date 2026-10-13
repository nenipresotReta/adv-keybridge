#pragma once
#include <stdbool.h>
bool zmk_keymap_layer_active(unsigned);
int zmk_keymap_layer_activate(unsigned,bool);
int zmk_keymap_layer_deactivate(unsigned,bool);
