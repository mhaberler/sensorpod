#if defined(USE_M5UNIFIED)
    #include <M5Unified.h>
#endif
#include <ImprovWiFiBLE.h>
#include <ImprovWiFiLibrary.h>
#include "Adafruit_VL53L0X.h"
#include <ArduinoJson.h>
#include <PicoMQTT.h>
#include <PicoWebsocket.h>
#include <WiFi.h>
#include <Wire.h>
#include <optional>
#include "credstore.hpp"
#include "led.hpp"
#include "listenv.hpp"

#ifdef BUILD_TAG
    #define FW_VERSION  BUILD_TAG
#else
    #define FW_VERSION  "0.0.1"
#endif
// start BLE provisioning service only afeter wifi connect failed
// p4 takes longer time to get set up
#ifdef CONFIG_IDF_TARGET_ESP32P4
    #define TIME_TO_CONNECT 15*1000
#else
    #define TIME_TO_CONNECT 8*1000
#endif

#define DEVICE_NAME "sensorpod" // The name of the sensor
#define DURATION 200            // mS

int numClicks;
Adafruit_VL53L0X lox = Adafruit_VL53L0X();
bool lox_present;

#include "mqtt.hpp"
ImprovWiFi improvSerial(&Serial);

extern String macAddress;
extern String hostName;

void wifi_setup(void);
void wifi_loop(void);
void startStaAttempt(const String &ssid, const String &pass);
void stopSta();
void button_setup(void);
void button_loop(void);
bool i2c_probe(TwoWire &w, uint8_t addr);
void i2c_scan(TwoWire &w);
bool lox_init(TwoWire &wire);
std::optional<uint16_t> lox_poll(TwoWire &wire);

void onImprovWiFiErrorCb(ImprovTypes::Error err) {
    log_e("Improv error %d", err);
    blinkLed(2000, 3);
}

void onImprovWiFiConnectedCb(const char *ssid, const char *password) {
    log_i("Improv provisioned ssid=%s", ssid);
    saveWiFiCredentials(ssid, password);
    blinkLed(100, 3);
    startStaAttempt(String(ssid), String(password));
}

void startImprovSerialProvisioning() {
    String mac;
    String devId = String(HOSTNAME) + "_" + macAddress;
    improvSerial.setDeviceInfo(ImprovTypes::ChipFamily::CF_ESP32, "ImprovWiFiLib", IMPROV_WIFI_LIBRARY_VERSION, devId.c_str(), "http://{LOCAL_IPV4}");
    improvSerial.onImprovError(onImprovWiFiErrorCb);
    improvSerial.onImprovConnected(onImprovWiFiConnectedCb);
}


void setup() {
    Serial.begin(115200);
    delay(3000);
    listEnv();
    hostName = WiFi.getHostname();
    ledSetup();

#if defined(USE_M5UNIFIED)
    auto cfg = M5.config();
    cfg.output_power = true;
    M5.begin(cfg);
    // M5.Ex_I2C.begin();
    // Wire.end();
    // Wire.begin(M5.Ex_I2C.getSDA(), M5.Ex_I2C.getSCL(), 100000);
#else
    Wire.begin(SDA_PIN,SCL_PIN, 400000);
#endif
    startImprovSerialProvisioning();
    button_setup();
    // i2c_scan(Wire);
    lox_present = lox_init(Wire);
    wifi_setup();
    mqtt.begin();
    blinkLed(100, 5);
}

void loop() {
    improvSerial.handleSerial();

    unsigned long now = millis();
    button_loop();
#if defined(USE_M5UNIFIED)
    M5.update();
#endif
    static unsigned long last_lox_poll = 0;
    if (now - last_lox_poll > DURATION) {
        auto range = lox_poll(Wire);
        last_lox_poll = now;
        if (range.has_value()) {
            JsonDocument doc;
            doc["distance_mm"] = *range;
            auto publish = mqtt.begin_publish("VL53L0X", measureJson(doc));
            serializeJson(doc, publish);
            publish.send();
        } else {
            numClicks = 0;
        }
    }

    static unsigned long lastStatusPublish = 0;
    if (now - lastStatusPublish > 1000) {
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
    wifi_loop();
    yield();
}

bool i2c_probe(TwoWire &w, uint8_t addr) {
    w.beginTransmission(addr);
    return (w.endTransmission() == 0);
}

void i2c_scan(TwoWire &w) {
    uint8_t bus = (&w == &Wire) ? 0 : 1;
    log_i("scanning Wire%u", bus);
    for (auto i = 8; i < 128; i++) {
        w.beginTransmission(i);
        if (w.endTransmission() == 0) {
            log_i("Wire%u dev at 0x%x", bus, i);
        }
    }
}

bool lox_init(TwoWire &wire) {
    bool lox_present = lox.begin();
    log_w("VL53L0X:%s detected", lox_present ? "" : " not");
    return lox_present;
}

std::optional<uint16_t> lox_poll(TwoWire &wire) {
    if (!lox_present)
        return std::nullopt;

    VL53L0X_RangingMeasurementData_t measure;
    lox.rangingTest(&measure, false);
    if (measure.RangeStatus != 4) {
        // log_d("range = %u", measure.RangeMilliMeter);
        return measure.RangeMilliMeter;
    }
    return std::nullopt;
}

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

#endif

void button_setup(void) {
#if defined(BUTTON_PIN)
    button.attachClick(singleClick);
    button.attachDoubleClick(doubleClick);
    button.attachMultiClick(multiClick);
    button.attachLongPressStart(longPressErase);
#endif
}

void button_loop(void) {
#if defined(BUTTON_PIN)
    button.tick();
#endif
}
