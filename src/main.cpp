#if defined(M5UNIFIED)
#include <M5Unified.h>
#endif
#include "BLEScanner.h"
#include "ESP_HostedOTA.h"
#include <ESPmDNS.h>
#include <PicoMQTT.h>
#include <PicoWebsocket.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <Wire.h>

#ifdef BOARD_HAS_PSRAM
#define RBMEM MALLOC_CAP_SPIRAM
#else
#define RBMEM MALLOC_CAP_DEFAULT
#endif

extern PicoMQTT::Server mqtt;
static auto &bleScanner = BLEScanner::instance();
extern bool decodedOnly;
extern uint8_t wifi_status;

void i2c_init(TwoWire &wire);
void i2c_poll(TwoWire &wire);
void wifi_setup();
void wifi_loop();
void websocket_setup();
void websocket_loop();

void setup() {
  Serial.begin(115200);
  delay(3000);
#if defined(M5UNIFIED)
  auto cfg = M5.config();
  cfg.output_power = true;
  M5.begin(cfg);
  M5.Ex_I2C.begin();
  Wire.end();
  Wire.begin(M5.Ex_I2C.getSDA(), M5.Ex_I2C.getSCL(), 100000);
#else
  Wire.begin();
#endif
  i2c_init(Wire);
  wifi_setup();
  bleScanner.begin(4096, 15000, 100, 99, 4096, 1, RBMEM);
}

void loop() {
  unsigned long now = millis();
#if defined(M5UNIFIED)
  M5.update();
#endif
  if (wifi_status == WL_CONNECTED) {
    {
      JsonDocument doc;
      char mac[16];
      if (bleScanner.process(doc, mac, sizeof(mac))) {
        // Only publish if not decodedOnly mode, or if decoded property is true
        if (!decodedOnly || doc["decoded"].as<bool>()) {
          // Remove decoded attribute in decodedOnly mode before publishing
          if (decodedOnly) {
            doc.remove("decoded");
          }
          String topic = String("ble/") + mac;
          auto publish = mqtt.begin_publish(topic.c_str(), measureJson(doc));
          serializeJson(doc, publish);
          publish.send();
        }
      }
    }
    static unsigned long lastI2CPublish = 0;
    if (now - lastI2CPublish >= 100) {
      i2c_poll(Wire);
      lastI2CPublish = now;
    }
    static unsigned long lastStatusPublish = 0;
    if (now - lastStatusPublish >= 1000) {
      lastStatusPublish = now;
      JsonDocument doc;
      doc["uptime"] = now / 1000;
      doc["cpu_temperature"] = temperatureRead();
      doc["rssi"] = WiFi.RSSI();

      auto publish = mqtt.begin_publish("status", measureJson(doc));
      serializeJson(doc, publish);
      publish.send();
    }
    mqtt.loop();
  }
  wifi_loop();
  yield();
}