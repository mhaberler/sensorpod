#include "button.hpp"
#include "credstore.hpp"
#include <Arduino.h>

extern int numClicks;
void stopSta();

#if defined(BUTTON_PIN)

#include "OneButton.h"
OneButton button(BUTTON_PIN, true,
                 true); // Button pin, active low, pullup enabled

void singleClick() {
  log_i("singleClick() detected.");
  numClicks = 1;
}

void doubleClick() {
  log_i("doubleClick() detected.");
  numClicks = 2;
}

void multiClick() {
  int n = button.getNumberClicks();
  log_i("%d clicks detected.", n);
  numClicks = n;
}

void longPressErase() {
  log_w("button long-press: erasing creds, AP-only mode");
  clearWiFiCredentials();
  stopSta();
}

#elif defined(BUTTON_SCENARIO_M5UNIFIED)

#include <M5Unified.h>

void longPressErase() {
  log_w("button long-press: erasing creds, AP-only mode");
  clearWiFiCredentials();
  stopSta();
}

#endif

void button_setup(void) {
#if defined(BUTTON_PIN)
  button.attachClick(singleClick);
  button.attachDoubleClick(doubleClick);
  button.attachMultiClick(multiClick);
  button.attachLongPressStart(longPressErase);
#elif defined(BUTTON_SCENARIO_M5UNIFIED)
  M5.BtnA.setHoldThresh(800); // match OneButton's default long-press threshold
#endif
}

void button_loop(void) {
#if defined(BUTTON_PIN)
  button.tick();
#elif defined(BUTTON_SCENARIO_M5UNIFIED)
  if (M5.BtnA.wasDecideClickCount()) {
    int n = M5.BtnA.getClickCount();
    switch (n) {
    case 1:
      log_i("singleClick() detected.");
      numClicks = 1;
      break;
    case 2:
      log_i("doubleClick() detected.");
      numClicks = 2;
      break;
    default:
      log_i("%d clicks detected.", n);
      numClicks = n;
      break;
    }
  }
  if (M5.BtnA.wasHold()) {
    longPressErase();
  }
#endif
}
