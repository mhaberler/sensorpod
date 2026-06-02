#include "led.hpp"
#include <Arduino.h>

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

#else

void ledSetup() {}

void blinkLed(int d, int times, uint32_t color) {
    (void)d;
    (void)times;
    (void)color;
}

#endif
