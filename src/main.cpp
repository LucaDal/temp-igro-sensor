#include "CommonDebug.h"
#include "DeviceSetupManager.h"
#include "LiteWiFiManager.h"
#include "MyDeviceProperties.h"
#include "PubSubClient.h"
#include "SHTSensor.h"
#include "cert.h"
#include "secret_data.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiClientSecureBearSSL.h>
#include <Wire.h>

#define LOG(msg) DBG_LOG("", msg)
#define LOGF(fmt, ...) DBG_LOGF("", fmt, ##__VA_ARGS__)

MyDeviceProperties deviceProps;
LiteWiFiManager wifiMgr;
DeviceSetupManager setupMgr;
const char *DEVICE_ID;
// 10 minutes
const int DEEPSLEEP = 600e6;
const int UPDATE_TIME = 600e6;

const char *mqtt_username;
const char *mqtt_password;
const char *mqtt_topic;

BearSSL::WiFiClientSecure esp_client;
PubSubClient mqtt_client(esp_client);

SHTSensor sht;

unsigned long update_time = 0;

void connectToMQTTBroker() {
  int n_try = 5;
  while (!mqtt_client.connected() && n_try > 0) {
    Serial.println("Connecting...");
    String client_id = "esp8266-" + String(WiFi.macAddress());
    if (mqtt_client.connect(client_id.c_str(), mqtt_username, mqtt_password)) {
      LOG("Connected to MQTT broker");
      mqtt_client.subscribe(mqtt_topic);
      break;
    } else {
      LOG("Failed connecting - retrying in 5 seconds");
      LOG(mqtt_client.state());
      delay(5000);
    }
    n_try--;
  }
  if (n_try <= 0 && DEEPSLEEP > 0)
    ESP.deepSleep(DEEPSLEEP);
}

void update() {
  JsonDocument doc;
  if (millis() > update_time) {
    update_time = millis() + UPDATE_TIME;
#ifdef DEBUG
    doc["temp"] = String(20.9, 1);
    doc["umid"] = String(89.55, 0);
#else
    sht.readSample();
    doc["temp"] = String(sht.getTemperature(), 1);
    doc["umid"] = String(sht.getHumidity(), 0);
#endif
    mqtt_client.publish(mqtt_topic, doc.as<String>().c_str());
    delay(500);
    if (DEEPSLEEP > 0)
      ESP.deepSleep(DEEPSLEEP);
  }
}
void setupMqtt() {
  mqtt_username = deviceProps.getProperty("username");
  mqtt_password = deviceProps.getProperty("password");
  mqtt_topic = deviceProps.getProperty("topic");
  LOGF("\tUsername [%s];\n\tPassword: [%s];\ntopic: [%s];\n", mqtt_username,
       mqtt_password, mqtt_topic);

  BearSSL::X509List *serverTrustedCA = new BearSSL::X509List(ssl_ca_cert);
  esp_client.setTrustAnchors(serverTrustedCA);
  esp_client.setBufferSizes(512, 512);
  mqtt_client.setServer(MQTT_BTOKER, 8883);
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

  // Setup device previously settled up
  size_t nextOffset = 0;
  setupMgr.begin();
  DEVICE_ID = setupMgr.readCString(0, &nextOffset);

  LOGF("\tDevice Id: [%s]", DEVICE_ID);
  if (WiFi.status() != WL_CONNECTED) {
    ESP.deepSleep(DEEPSLEEP);
  }
  deviceProps.begin(PORTAL_SERVER_IP, DEVICE_ID, nextOffset);
  deviceProps.fetchAndStoreIfChanged();

  setupMqtt();
}

void loop() {
  if (!mqtt_client.connected()) {
    connectToMQTTBroker();
  } else {
    update();
  }
  mqtt_client.loop();
}
