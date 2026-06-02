#pragma once
#include <cstdint>

void ledSetup();
void blinkLed(int d, int times, uint32_t color = 0xFFFFFF);
