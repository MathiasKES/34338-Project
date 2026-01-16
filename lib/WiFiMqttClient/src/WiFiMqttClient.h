#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFiClient.h>


class WifiMqttClient {
public:
  WifiMqttClient();

  void begin(
    const char* wifiSsid,
    const char* wifiPass,
    const char* mqttHost,
    uint16_t mqttPort,
    const char* mqttUser,
    const char* mqttPass,
    const char* deviceId,
    const char* site
  );

  void loop();
  bool connected();

  bool publishJson(
    const char* topicSuffix,
    const JsonDocument& data
  );

  void setCallback(MQTT_CALLBACK_SIGNATURE);

  bool subscribe(const char* topic);
  bool unsubscribe(const char* topic);

  String makeTopic(const char* suffix) const;

private:
  void connectWifi();
  void connectMqtt();
  
  WiFiClient wifiClient;
  PubSubClient mqtt;

  const char* wifiSsid;
  const char* wifiPass;
  const char* mqttHost;
  const char* mqttUser;
  const char* mqttPass;
  const char* deviceId;
  const char* site;

  uint16_t mqttPort;
  String baseTopic;
  uint64_t chipId;
};
