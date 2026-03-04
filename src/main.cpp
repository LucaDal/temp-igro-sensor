#include "CommonDebug.h"
#include "DeviceSetupManager.h"
#include "LiteWiFiManager.h"
#include "MQTTManager.h"
#include "MyDeviceProperties.h"
#include "SHTSensor.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Wire.h>

#define LOG(msg) DBG_LOG("", msg)
#define LOGF(fmt, ...) DBG_LOGF("", fmt, ##__VA_ARGS__)

MyDeviceProperties deviceProps;
LiteWiFiManager wifiMgr;
DeviceSetupManager setupMgr;
MQTTManager mqtt;
String mqttTopic;
uint64_t sleepTimeUs = 300000000ULL;
bool runtimeConfigured = false;

SHTSensor sht;

constexpr int kMaxMqttConnectTries = 2;
constexpr int kMqttRetryDelayMs = 1200;

void connectToMQTTBroker() {
  if (WiFi.status() != WL_CONNECTED) {
    LOG("WiFi not connected, skipping MQTT connect");
    return;
  }

  int n_try = kMaxMqttConnectTries;
  while (!mqtt.connected() && n_try > 0) {
    String client_id = "esp8266-" + String(WiFi.macAddress());
    if (mqtt.connect(client_id.c_str())) {
      LOG("Connected to MQTT broker");
      break;
    } else {
      LOGF("MQTT connect failed, rc=%d\n", mqtt.state());
      delay(kMqttRetryDelayMs);
    }
    n_try--;
  }

  if (n_try <= 0) {
    LOG("MQTT connection retries exhausted, sleeping");
    ESP.deepSleep(sleepTimeUs);
  }
}

bool publishSensorSample() {
  if (!mqtt.connected()) {
    LOG("Publish skipped: MQTT disconnected");
    return false;
  }

  JsonDocument doc;
#ifdef DEBUG
  doc["temp"] = String(20.9, 1);
  doc["umid"] = String(89.55, 0);
#else
  sht.readSample();
  doc["temp"] = String(sht.getTemperature(), 1);
  doc["umid"] = String(sht.getHumidity(), 0);
#endif

  String payload = doc.as<String>();
  bool published = mqtt.publish(mqttTopic.c_str(), payload.c_str());
  LOGF("Publishing message: %s\n", payload.c_str());
  if (!published) {
    LOGF("Publish failed, rc=%d\n", mqtt.state());
  }

  return published;
}

void update() {
  publishSensorSample();
  mqtt.loop();
  delay(200);
  mqtt.disconnect();
  delay(100);
  ESP.deepSleep(sleepTimeUs);
}

void setup() {
#ifdef DEBUG
  Serial.begin(115200);
#else
  Wire.begin(2, 0); // sda - scl
  sht.init();
#endif
  LOG("Starting\n");
  wifiMgr.begin("TermIgro Sensor");
  if (!setupMgr.begin()) {
    LOG("DeviceSetupManager begin failed\n");
    return;
  }

  if (WiFi.status() == WL_CONNECTED && strlen(setupMgr.deviceId()) > 0 &&
      strlen(setupMgr.deviceSecret()) > 0 &&
      strlen(setupMgr.deviceTypeId()) > 0 &&
      strlen(setupMgr.portalServerIp()) > 0) {
    deviceProps.begin(setupMgr.portalServerIp(), setupMgr.deviceId(),
                      setupMgr.deviceSecret());
    deviceProps.fetchAndStoreIfChanged();
    if (mqtt.begin(deviceProps.Get("MQTT_BROKER"),
                   deviceProps.GetInt("MQTT_PORT", 8883))) {
      connectToMQTTBroker();
    }
    mqttTopic = deviceProps.Get("topic", "");
    sleepTimeUs =
        static_cast<uint64_t>(deviceProps.GetInt("updateTime", 300)) * 1000000ULL;
    // min is 5 minutes
    if (sleepTimeUs < 300000000ULL) {
      sleepTimeUs = 300000000ULL;
    }
    runtimeConfigured = !mqttTopic.isEmpty();
  }
}

void loop() {
  if (!runtimeConfigured) {
    LOG("Runtime not configured, sleeping");
    ESP.deepSleep(sleepTimeUs);
  }

  if (WiFi.status() != WL_CONNECTED) {
    LOG("WiFi disconnected, sleeping");
    ESP.deepSleep(sleepTimeUs);
  }

  if (!mqtt.connected()) {
    connectToMQTTBroker();
  } else {
    update();
  }
  mqtt.loop();
}
