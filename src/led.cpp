#include "led.hpp"
#include <Arduino.h>

#if __has_include(<M5Unified.h>)
#define HAS_M5UNIFIED
#include <M5Unified.h>
#endif

static LEDState _led_state = LED_OFF;
static unsigned long _led_last_toggle = 0;
const unsigned long LED_FAST_BLINK_INTERVAL = 200;   // WiFi down
const unsigned long LED_SLOW_BLINK_INTERVAL = 500;   // Broker down

#if defined(LED_SCENARIO_RGB)
#include <Adafruit_NeoPixel.h>
#if defined(RGB_LED_TYPE_SK6812)
#define _LED_NEO_TYPE (NEO_GRBW + NEO_KHZ800)
#else
#define _LED_NEO_TYPE (NEO_GRB + NEO_KHZ800)
#endif

static Adafruit_NeoPixel _led_pixel(1, RGB_LED_PIN, _LED_NEO_TYPE);

void ledSetup() {
    _led_pixel.begin();
    _led_pixel.show();
}

void blinkLed(int d, int times, uint32_t color) {
    for (int j = 0; j < times; j++) {
        _led_pixel.setPixelColor(0, color);
        _led_pixel.show();
        delay(d);
        _led_pixel.setPixelColor(0, 0);
        _led_pixel.show();
        delay(d);
    }
}

#elif defined(LED_SCENARIO_SINGLE)

void ledSetup() {
    pinMode(LED_PIN, OUTPUT);
}

void blinkLed(int d, int times, uint32_t color) {
    (void)color;
    for (int j = 0; j < times; j++) {
        digitalWrite(LED_PIN, HIGH);
        delay(d);
        digitalWrite(LED_PIN, LOW);
        delay(d);
    }
}

#elif defined(LED_SCENARIO_SINGLE_M5UNIFIED)

static bool _m5_led_on = false;

void ledSetup() {
    // M5.begin() already configures the power LED; nothing to do here.
}

void blinkLed(int d, int times, uint32_t color) {
    (void)color;
    for (int j = 0; j < times; j++) {
        M5.Power.setLed(255);
        delay(d);
        M5.Power.setLed(0);
        delay(d);
    }
    _m5_led_on = false;
}

#else

void ledSetup() {}

void blinkLed(int d, int times, uint32_t color) {
    (void)d;
    (void)times;
    (void)color;
}

#endif

// LED status feedback: WiFi/broker connection state
void updateLed(LEDState state) {
    _led_state = state;
    _led_last_toggle = millis();
}

void ledLoop() {
    unsigned long now = millis();

    switch (_led_state) {
        case LED_OFF:
#if defined(LED_SCENARIO_RGB)
            _led_pixel.setPixelColor(0, 0);
            _led_pixel.show();
#elif defined(LED_SCENARIO_SINGLE)
            digitalWrite(LED_PIN, LOW);
#elif defined(LED_SCENARIO_SINGLE_M5UNIFIED)
            M5.Power.setLed(0);
            _m5_led_on = false;
#endif
            break;

        case LED_SOLID:
#if defined(LED_SCENARIO_RGB)
            _led_pixel.setPixelColor(0, 0x00FF00);  // Green
            _led_pixel.show();
#elif defined(LED_SCENARIO_SINGLE)
            digitalWrite(LED_PIN, HIGH);
#elif defined(LED_SCENARIO_SINGLE_M5UNIFIED)
            M5.Power.setLed(255);
            _m5_led_on = true;
#endif
            break;

        case LED_SLOW_BLINK:  // Broker down
            if (now - _led_last_toggle > LED_SLOW_BLINK_INTERVAL) {
#if defined(LED_SCENARIO_RGB)
                uint32_t color = (_led_pixel.getPixelColor(0) == 0) ? 0xFF8800 : 0;  // Orange/off
                _led_pixel.setPixelColor(0, color);
                _led_pixel.show();
#elif defined(LED_SCENARIO_SINGLE)
                digitalWrite(LED_PIN, !digitalRead(LED_PIN));
#elif defined(LED_SCENARIO_SINGLE_M5UNIFIED)
                _m5_led_on = !_m5_led_on;
                M5.Power.setLed(_m5_led_on ? 255 : 0);
#endif
                _led_last_toggle = now;
            }
            break;

        case LED_FAST_BLINK:  // WiFi down
            if (now - _led_last_toggle > LED_FAST_BLINK_INTERVAL) {
#if defined(LED_SCENARIO_RGB)
                uint32_t color = (_led_pixel.getPixelColor(0) == 0) ? 0xFF0000 : 0;  // Red/off
                _led_pixel.setPixelColor(0, color);
                _led_pixel.show();
#elif defined(LED_SCENARIO_SINGLE)
                digitalWrite(LED_PIN, !digitalRead(LED_PIN));
#elif defined(LED_SCENARIO_SINGLE_M5UNIFIED)
                _m5_led_on = !_m5_led_on;
                M5.Power.setLed(_m5_led_on ? 255 : 0);
#endif
                _led_last_toggle = now;
            }
            break;
    }
}
