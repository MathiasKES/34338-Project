/**
 * @file WiFiMqttClient.h
 * @brief Lightweight WiFi + MQTT helper wrapper for ESP-based Arduino systems.
 *
 * @defgroup infrastructure Infrastructure – WiFi & MQTT
 * @{
 *
 * @details
 * This header defines the WifiMqttClient class, which encapsulates:
 * - WiFi connection management
 * - MQTT connection and reconnection logic
 * - Topic namespace handling (site / device scoping)
 * - JSON-based MQTT publish helpers
 *
 * The class is designed to simplify MQTT usage in distributed embedded systems
 * by providing a consistent base topic structure and robust connection handling.
 */


#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFiClient.h>

/**
 * @class WifiMqttClient
 * @brief Combined WiFi and MQTT client abstraction.
 *
 * This class wraps an Arduino WiFiClient and PubSubClient to provide:
 * - Automatic WiFi and MQTT reconnection
 * - Topic construction based on site and device identifiers
 * - JSON publishing convenience functions
 *
 * Typical base topic structure:
 *   <mqttUser>/<site>/<deviceId>/<suffix>
 */
class WifiMqttClient {
public:
  /**
   * @brief Default constructor.
   *
   * Initializes internal state but does not establish
   * any network connections.
   */
  WifiMqttClient();

  /**
   * @brief Initializes WiFi and MQTT configuration.
   *
   * Stores credentials and connection parameters, initializes
   * internal clients, and prepares base topic paths.
   *
   * @param wifiSsid WiFi network SSID.
   * @param wifiPass WiFi network password.
   * @param mqttHost MQTT broker hostname or IP.
   * @param mqttPort MQTT broker port.
   * @param mqttUser MQTT username.
   * @param mqttPass MQTT password.
   * @param deviceId Unique device identifier (e.g. "door1").
   * @param site Site or location identifier (e.g. "site1").
   */
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

  /**
   * @brief Main service loop.
   *
   * Must be called frequently from the Arduino loop().
   * Handles:
   * - WiFi reconnection
   * - MQTT reconnection
   * - MQTT client loop processing
   */
  void loop();

  /**
   * @brief Checks whether the MQTT client is currently connected.
   *
   * @return true if MQTT connection is active, false otherwise.
   */
  bool connected();

  /**
   * @brief Publishes a JSON document to an MQTT topic.
   *
   * Automatically prefixes the topic with the base topic
   * (<user>/<site>/<deviceId>/).
   *
   * @param topicSuffix Topic suffix appended to the base topic.
   * @param data JSON document to serialize and publish.
   * @return true if publish succeeded, false otherwise.
   */
  bool publishJson(
    const char* topicSuffix,
    const JsonDocument& data
  );

  /**
   * @brief Sets the MQTT message callback.
   *
   * The callback is invoked when subscribed messages are received.
   *
   * @param MQTT_CALLBACK_SIGNATURE Function pointer matching PubSubClient callback signature.
   */
  void setCallback(MQTT_CALLBACK_SIGNATURE);

  /**
   * @brief Subscribes to a topic.
   *
   * The provided topic should already be fully constructed
   * (use makeTopic() if needed).
   *
   * @param topic Full MQTT topic string.
   * @return true if subscription succeeded, false otherwise.
   */
  bool subscribe(const char* topic);

  /**
   * @brief Unsubscribes from a topic.
   *
   * @param topic Full MQTT topic string.
   * @return true if unsubscribe succeeded, false otherwise.
   */
  bool unsubscribe(const char* topic);

  /**
   * @brief Constructs a fully qualified MQTT topic.
   *
   * Combines the base topic with a suffix:
   *   <user>/<site>/<deviceId>/<suffix>
   *
   * @param suffix Topic suffix (e.g. "access/request").
   * @return Constructed topic as an Arduino String.
   */
  String makeTopic(const char* suffix) const;

private:
  /**
   * @brief Establishes a WiFi connection if not already connected.
   *
   * Handles blocking connection attempts and retries.
   */
  void connectWifi();

  /**
   * @brief Establishes an MQTT connection if not already connected.
   *
   * Uses stored credentials and base topic information.
   */
  void connectMqtt();

  // ---------------------------------------------------------------------------
  // Internal clients
  // ---------------------------------------------------------------------------

  /** @brief Underlying WiFi client used by the MQTT client. */
  WiFiClient wifiClient;

  /** @brief PubSubClient instance handling MQTT protocol. */
  PubSubClient mqtt;

  // ---------------------------------------------------------------------------
  // Stored configuration parameters
  // ---------------------------------------------------------------------------

  /** @brief WiFi SSID. */
  const char* wifiSsid;

  /** @brief WiFi password. */
  const char* wifiPass;

  /** @brief MQTT broker hostname or IP address. */
  const char* mqttHost;

  /** @brief MQTT username. */
  const char* mqttUser;

  /** @brief MQTT password. */
  const char* mqttPass;

  /** @brief Device identifier used in topic hierarchy. */
  const char* deviceId;

  /** @brief Site identifier used in topic hierarchy. */
  const char* site;

  /** @brief MQTT broker port number. */
  uint16_t mqttPort;

  // ---------------------------------------------------------------------------
  // Derived and runtime state
  // ---------------------------------------------------------------------------

  /**
   * @brief Base MQTT topic prefix.
   *
   * Format:
   *   <mqttUser>/<site>/<deviceId>
   */
  String baseTopic;

  /**
   * @brief Unique chip identifier.
   *
   * Used to generate unique MQTT client IDs.
   */
  uint64_t chipId;
};
