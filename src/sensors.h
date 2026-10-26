// KNOMI's own sensors: SHT4x (air temperature/humidity) and LIS2DW12
// accelerometer (knocks wake the face).
#pragma once
#include "state.h"

void sensors_begin();
// Polls the sensors; fills the air readings and returns true on a knock.
bool sensors_loop(uint32_t now, PrinterState &s);
