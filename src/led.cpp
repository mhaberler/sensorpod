// Status LED via the StatusLed library: a dedicated task drives the LED, so
// blinkLed() and updateLed() never block loop() (or Improv/BLE callbacks on
// other tasks). Colors and timings match the previous blocking implementation.
#include "led.hpp"
#include <Arduino.h>
#include <StatusLed.h>
#include <StatusLedAdapters.h>

#if __has_include(<M5Unified.h>)
#define HAS_M5UNIFIED
#include <M5Unified.h>
#endif

using namespace statusled;

static StatusLed _led;

// Status patterns (same as the former ledLoop() state machine)
constexpr Step PAT_SOLID[] = {{0x00FF00, 1000}};               // green
constexpr Step PAT_SLOW[] = {{0xFF8800, 500}, {0, 500}};       // broker down
constexpr Step PAT_FAST[] = {{0xFF0000, 200}, {0, 200}};       // WiFi down
constexpr Step PAT_OFF[] = {{0, 1000}};

#if defined(LED_SCENARIO_RGB)
#if defined(RGB_LED_TYPE_SK6812)
#define _LED_NEO_TYPE (NEO_GRBW + NEO_KHZ800)
#else
#define _LED_NEO_TYPE (NEO_GRB + NEO_KHZ800)
#endif
#if !defined(RGB_POWER_PIN)
#define RGB_POWER_PIN -1
#endif
#if !defined(RGB_POWER_ENABLE)
#define RGB_POWER_ENABLE HIGH
#endif
static NeoPixelLed _hw(RGB_LED_PIN, _LED_NEO_TYPE, 1, RGB_POWER_PIN,
                       RGB_POWER_ENABLE);

void ledSetup() {
  _hw.begin();
  _led.begin(NeoPixelLed::write, &_hw);
}

#elif defined(LED_SCENARIO_SINGLE)

#if defined(LED_ACTIVE_LOW)
static GpioLed _hw{LED_PIN, true};
#else
static GpioLed _hw{LED_PIN, false};
#endif

void ledSetup() {
  _hw.begin();
  _led.begin(GpioLed::write, &_hw);
}

#elif defined(LED_SCENARIO_SINGLE_M5UNIFIED)

// Full on/off like before (the adapter's luminance mapping would dim red/orange).
static void m5LedWrite(uint32_t rgb, void *) { M5.Power.setLed(rgb ? 255 : 0); }

void ledSetup() {
  // M5.begin() already configured the power LED.
  _led.begin(m5LedWrite);
}

#else

void ledSetup() {} // no LED: StatusLed not started, calls below are no-ops

#endif

// Non-blocking: queues a one-shot event (times x on d ms / off d ms).
void blinkLed(int d, int times, uint32_t color) {
  if (times > 0 && d > 0)
    _led.blink(color, d > 0xFFFF ? 0xFFFF : d, times > 255 ? 255 : times);
}

void updateLed(LEDState state) {
  switch (state) {
  case LED_OFF:
    _led.setStatus(pattern(PAT_OFF));
    break;
  case LED_SOLID:
    _led.setStatus(pattern(PAT_SOLID));
    break;
  case LED_SLOW_BLINK:
    _led.setStatus(pattern(PAT_SLOW));
    break;
  case LED_FAST_BLINK:
    _led.setStatus(pattern(PAT_FAST));
    break;
  }
}

// Kept for compatibility: the StatusLed task does the timing now.
void ledLoop() {}
