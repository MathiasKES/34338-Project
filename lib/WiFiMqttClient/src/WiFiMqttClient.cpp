/**
 * @file WiFiMqttClient.cpp
 * @brief Implementation of the WifiMqttClient helper class.
 *
 * @ingroup infrastructure
 */


#include "WiFiMqttClient.h"
#include <ESP8266WiFi.h>
// #include <esp_system.h>

/**
 * @brief Human-readable device platform name.
 *
 * Included in published MQTT metadata.
 */
static constexpr const char* DEVICE_NAME = "ESP8266";

/**
 * @brief Default constructor.
 *
 * Initializes the PubSubClient instance with the internal WiFiClient.
 */
WifiMqttClient::WifiMqttClient()
  : mqtt(wifiClient) {}

/**
 * @brief Initializes WiFi and MQTT configuration.
 *
 * Stores provided credentials, prepares base topic structure,
 * configures WiFi and MQTT clients, and performs initial connections.
 *
 * @param wifiSsid WiFi network SSID.
 * @param wifiPass WiFi network password.
 * @param mqttHost MQTT broker hostname.
 * @param mqttPort MQTT broker port.
 * @param mqttUser MQTT username.
 * @param mqttPass MQTT password.
 * @param deviceId Device identifier used in topic hierarchy.
 * @param site Site identifier used in topic hierarchy.
 */
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
  // Store configuration parameters
  this->wifiSsid  = wifiSsid;
  this->wifiPass  = wifiPass;
  this->mqttHost  = mqttHost;
  this->mqttPort  = mqttPort;
  this->mqttUser  = mqttUser;
  this->mqttPass  = mqttPass;
  this->deviceId  = deviceId;
  this->site      = site;

  // Retrieve unique chip identifier for client ID generation
  chipId = ESP.getChipId();

  // Construct base MQTT topic: <user>/<site>/<device>
  baseTopic = String(mqttUser) + "/" + site + "/" + deviceId;

  // Configure WiFi and MQTT clients
  WiFi.mode(WIFI_STA);
  mqtt.setServer(mqttHost, mqttPort);

  // Increase MQTT buffer to support JSON payloads
  mqtt.setBufferSize(1024);

  // Perform initial connections
  connectWifi();
  connectMqtt();
}

/**
 * @brief Main service loop.
 *
 * Ensures WiFi and MQTT connections remain active
 * and processes incoming MQTT messages.
 */
void WifiMqttClient::loop() {

  // Reconnect WiFi if connection was lost
  if (WiFi.status() != WL_CONNECTED) {
    connectWifi();
  }

  // Reconnect MQTT if disconnected
  if (!mqtt.connected()) {
    connectMqtt();
  }

  // Process MQTT client state machine
  mqtt.loop();
}

/**
 * @brief Checks whether the MQTT client is connected.
 *
 * @return true if connected, false otherwise.
 */
bool WifiMqttClient::connected() {
  return mqtt.connected();
}

/**
 * @brief Establishes a WiFi connection.
 *
 * Blocks until connected or timeout occurs.
 * Safe to call repeatedly.
 */
void WifiMqttClient::connectWifi() {

    Serial.println();
    Serial.println("=== WiFi: connect start ===");
    Serial.print("SSID: ");
    Serial.println(wifiSsid);

    // Abort if already connected
    if (WiFi.status() == WL_CONNECTED) return;

    Serial.print("WiFi mode: ");
    Serial.println(WIFI_STA ? "STA" : "UNKNOWN");

    Serial.println("Calling WiFi.begin()");

    // Start WiFi connection attempt
    WiFi.begin(wifiSsid, wifiPass);
    
    unsigned long start = millis();
    uint8_t dots = 0;

    // Wait until connected or timeout
    while (WiFi.status() != WL_CONNECTED) {
      Serial.print(".");
      dots++;

      // Print newline every 16 dots for readability
      if (dots % 16 == 0) {
        Serial.println();
      }

      // Abort after 15 seconds to avoid permanent blocking
      if (millis() - start > 15000) {
        Serial.println();
        Serial.println("WiFi connect timeout (15s)");
        return;
      }

      delay(500);
    }
}

/**
 * @brief Establishes an MQTT connection.
 *
 * Blocks until connected. Generates a unique
 * client ID based on device and chip identifiers.
 */
void WifiMqttClient::connectMqtt() {

  // Retry until MQTT connection succeeds
  while (!mqtt.connected()) {

    // Construct unique MQTT client ID
    String clientId =
      String(DEVICE_NAME) + "_" +
      deviceId +
      "_" +
      String((uint32_t)chipId, HEX);

    // Attempt MQTT connection using credentials
    mqtt.connect(clientId.c_str(), mqttUser, mqttPass);

    // Wait before retrying on failure
    if (!mqtt.connected()) {
      delay(2000);
    }
  }
}

/**
 * @brief Constructs a fully qualified MQTT topic.
 *
 * Appends a suffix to the base topic:
 *   <user>/<site>/<deviceId>/<suffix>
 *
 * @param suffix Topic suffix.
 * @return Constructed topic as an Arduino String.
 */
String WifiMqttClient::makeTopic(const char* suffix) const {
  return baseTopic + "/" + suffix;
}

/**
 * @brief Publishes a JSON document to an MQTT topic.
 *
 * Wraps the provided JSON data in a standard envelope
 * containing device metadata and a timestamp.
 *
 * @param topicSuffix Topic suffix appended to the base topic.
 * @param data JSON document containing application payload.
 * @return true if publish succeeded, false otherwise.
 */
bool WifiMqttClient::publishJson(
  const char* topicSuffix,
  const JsonDocument& data
) {
  StaticJsonDocument<256> envelope;

  // Embed device metadata
  JsonObject device = envelope.createNestedObject("device");
  device["id"]       = deviceId;
  device["platform"] = DEVICE_NAME;
  device["chip_id"]  = String((uint32_t)chipId, HEX);

  // Attach timestamp and payload
  envelope["sent_ts_ms"] = millis();
  envelope["data"]       = data;

  // Serialize JSON into a temporary buffer
  char buffer[512];
  size_t len = serializeJson(envelope, buffer);

  // Publish serialized payload
  return mqtt.publish(
    makeTopic(topicSuffix).c_str(),
    buffer,
    len
  );
}

/**
 * @brief Sets the MQTT message callback.
 *
 * @param MQTT_CALLBACK_SIGNATURE Callback function pointer.
 */
void WifiMqttClient::setCallback(MQTT_CALLBACK_SIGNATURE) {
  mqtt.setCallback(callback);
}

/**
 * @brief Subscribes to an MQTT topic.
 *
 * @param topic Full MQTT topic string.
 * @return true if subscription succeeded, false otherwise.
 */
bool WifiMqttClient::subscribe(const char* topic) {
  if (!mqtt.connected()) return false;
  return mqtt.subscribe(topic);
}

/**
 * @brief Unsubscribes from an MQTT topic.
 *
 * @param topic Full MQTT topic string.
 * @return true if unsubscribe succeeded, false otherwise.
 */
bool WifiMqttClient::unsubscribe(const char* topic) {
  if (!mqtt.connected()) return false;
  return mqtt.unsubscribe(topic);
}
