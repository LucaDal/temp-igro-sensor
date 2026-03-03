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
// 10 minutes
const char *mqttTopic;
int sleepTimeSec;
int updateTime;

SHTSensor sht;

void connectToMQTTBroker() {
  int n_try = 2;
  while (!mqtt.connected() && n_try > 0) {
    Serial.println("Connecting...");
    String client_id = "esp8266-" + String(WiFi.macAddress());
    if (mqtt.connect(client_id.c_str())) {
      LOG("Connected to MQTT broker");
      break;
    } else {
      LOGF("MQTT connect failed, rc=%d\n", mqtt.state());
      delay(5000);
    }
    n_try--;
  } // parsing to microseconds
  if (n_try <= 0) {
    ESP.deepSleep(sleepTimeSec);
  }
}

void update() {
  JsonDocument doc;
#ifdef DEBUG
  doc["temp"] = String(20.9, 1);
  doc["umid"] = String(89.55, 0);
#else
  sht.readSample();
  doc["temp"] = String(sht.getTemperature(), 1);
  doc["umid"] = String(sht.getHumidity(), 0);
#endif
  mqtt.publish(mqttTopic, doc.as<String>().c_str());
  LOGF("Publishing message: %s", doc.as<String>().c_str());
  delay(100);
  mqtt.disconnect();
  ESP.deepSleep(sleepTimeSec);
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
    mqttTopic = deviceProps.Get("topic");
    sleepTimeSec = deviceProps.GetInt("updateTime") * 1000000;
    // min is 5 minutes
    sleepTimeSec = sleepTimeSec < 300e6 ? 300e6 : sleepTimeSec;
  }
}

void loop() {
  if (!mqtt.connected()) {
    connectToMQTTBroker();
  } else {
    update();
  }
  mqtt.loop();
}
