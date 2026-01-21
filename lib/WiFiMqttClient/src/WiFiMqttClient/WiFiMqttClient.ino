#include "WiFiMqttClient.h"
#include <ESP8266WiFi.h>
// #include <esp_system.h>

static constexpr const char* DEVICE_NAME = "ESP8266";

WifiMqttClient::WifiMqttClient()
  : mqtt(wifiClient) {}

void WifiMqttClient::begin(
  const char* wifiSsid,
  const char* wifiPass,
  const char* mqttHost,
  uint16_t mqttPort,
  const char* mqttUser,
  const char* mqttPass,
  const char* deviceId,
  const char* site
) {
  this->wifiSsid = wifiSsid;
  this->wifiPass = wifiPass;
  this->mqttHost = mqttHost;
  this->mqttPort = mqttPort;
  this->mqttUser = mqttUser;
  this->mqttPass = mqttPass;
  this->deviceId = deviceId;
  this->site = site;

  chipId = ESP.getChipId();

  baseTopic = String(mqttUser) + "/" + site + "/" + deviceId;

  WiFi.mode(WIFI_STA);
  mqtt.setServer(mqttHost, mqttPort);
  mqtt.setBufferSize(1024);

  connectWifi();
  connectMqtt();
}

void WifiMqttClient::loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWifi();
  }
  if (!mqtt.connected()) {
    connectMqtt();
  }
  mqtt.loop();
}

bool WifiMqttClient::connected() {
  return mqtt.connected();
}

void WifiMqttClient::connectWifi() {
    Serial.println();
    Serial.println("=== WiFi: connect start ===");
    Serial.print("SSID: ");
    Serial.println(wifiSsid);

    if (WiFi.status() == WL_CONNECTED) return;

    Serial.print("WiFi mode: ");
    Serial.println(WIFI_STA ? "STA" : "UNKNOWN");

    Serial.println("Calling WiFi.begin()");

    WiFi.begin(wifiSsid, wifiPass);
    
    unsigned long start = millis();
    uint8_t dots = 0;

    while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    dots++;

    if (dots % 16 == 0) {
        Serial.println();
    }

    if (millis() - start > 15000) {
        Serial.println();
        Serial.println("WiFi connect timeout (15s)");
        return;
    }

    delay(500);
    }

}

void WifiMqttClient::connectMqtWifiMqttClientt() {
  while (!mqtt.connected()) {
    String clientId =
      String(DEVICE_NAME) + "_" +
      deviceId +
      "_" +
      String((uint32_t)chipId, HEX);

    mqtt.connect(clientId.c_str(), mqttUser, mqttPass);
    if (!mqtt.connected()) {
      delay(2000);
    }
  }
}

String WifiMqttClient::makeTopic(const char* suffix) const {
  return baseTopic + "/" + suffix; // /events
}

bool WifiMqttClient::publishJson(
  const char* topicSuffix,
  const JsonDocument& data
) {
  StaticJsonDocument<256> envelope;

  JsonObject device = envelope.createNestedObject("device");
  device["id"] = deviceId;
  device["platform"] = DEVICE_NAME;
  device["chip_id"] = String((uint32_t)chipId, HEX);

  envelope["ts"] = millis() / 1000;
  envelope["data"] = data;

  char buffer[512];
  size_t len = serializeJson(envelope, buffer);

  return mqtt.publish(
    makeTopic(topicSuffix).c_str(),
    buffer,
    len
  );
}
