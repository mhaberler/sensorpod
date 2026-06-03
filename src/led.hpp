#pragma once
#include <cstdint>

enum LEDState {
  LED_OFF = 0,
  LED_SOLID,
  LED_SLOW_BLINK,
  LED_FAST_BLINK,
};

void ledSetup();
void blinkLed(int d, int times, uint32_t color = 0xFFFFFF);
void updateLed(LEDState state);
void ledLoop();
