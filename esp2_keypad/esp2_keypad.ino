/**
 * @file esp2_keypad.ino
 * @brief Keypad-based PIN entry controller with MQTT backend integration.
 *
 * @defgroup esp2 ESP2 - Keypad
 * @{
 *
 * @details
 * This firmware runs on an ESP-based Arduino-compatible board and implements
 * the second stage of the access control system.
 *
 * Hardware components:
 * - 4x4 matrix keypad using the Keypad library
 *
 * Functional responsibilities:
 * - Accepts PIN input from the user
 * - Provides keypress feedback (beeps)
 * - Publishes PIN entry progress and submissions via MQTT
 *
 * The keypad is only active after successful RFID authentication,
 * which is signaled asynchronously via MQTT.
 */


#include <Arduino.h>
#include <Keypad.h>
// #include <string.h>
#include <ArduinoJson.h>

#include <WiFiMqttClient.h>

// ---------------- Network configuration ----------------

/** @brief WiFi + MQTT client wrapper */
WifiMqttClient net;

/** @brief WiFi SSID */
constexpr char WIFI_SSID[] = "Mathias2.4";
/** @brief WiFi password */
constexpr char WIFI_PASS[] = "mrbombasticcallmefantastic";

/** @brief MQTT broker hostname */
constexpr char MQTT_HOST[] = "maqiatto.com";
/** @brief MQTT broker port */
constexpr uint16_t MQTT_PORT = 1883;
/** @brief MQTT username */
constexpr char MQTT_USER[] = "hectorfoss@gmail.com";
/** @brief MQTT password */
constexpr char MQTT_PASS[] = "potter";

/** @brief Unique device identifier used in MQTT topics */
constexpr char DEVICE_ID[] = "door1";

// ---------------- Keypad configuration ----------------

/** @brief Number of rows in the keypad matrix */
const byte ROWS = 4;

/** @brief Number of columns in the keypad matrix */
const byte COLS = 4;

/**
 * @brief Logical keypad layout.
 *
 * Defines the character returned for each row/column intersection.
 */
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};


/**
 * @brief GPIO pins connected to keypad rows.
 *
 * Order must match ROWS definition.
 */
byte rowPins[ROWS] = {16, 5, 4, 0}; /**< D0, D1, D2, D3 */

/**
 * @brief GPIO pins connected to keypad columns.
 *
 * Order must match COLS definition.
 */
byte colPins[COLS] = {2, 14, 12, 13}; /**< D4, D5, D6, D7 */

/*
 * Physical wiring reference:
 * R1 -> D8, R2 -> D7, R3 -> D6, R4 -> D5
 * C1 -> D4, C2 -> D3, C3 -> D2, C4 -> D1
 *
 * ESP8266 mapping:
 * D1=5, D2=4, D3=0, D4=2,
 * D5=14, D6=12, D7=13, D8=15
 */

/**
 * @brief Keypad instance handling scanning and debouncing.
 */
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ---------------- Code handling ----------------

/** @brief Required PIN length */
constexpr uint8_t CODE_LENGTH = 4;

/**
 * @brief Input buffer for PIN digits.
 *
 * One extra byte is reserved for the null terminator.
 */
char input[CODE_LENGTH + 1] = {0};

/** @brief Current index into the PIN buffer */
uint8_t currentIdx = 0;

/**
 * @brief Indicates whether keypad input is currently enabled.
 *
 * This flag is controlled via MQTT messages from the access controller.
 */
bool kpEnabled = false;

// -----------------------------------------------------------------------------
// MQTT callback
// -----------------------------------------------------------------------------

/**
 * @brief MQTT message callback handler.
 *
 * Handles:
 * - Enabling keypad input after RFID access is granted
 * - Disabling keypad input after PIN validation completes
 *
 * @param topic MQTT topic string.
 * @param payload Raw payload bytes.
 * @param length Payload length.
 */
void callback(char* topic, byte* payload, unsigned int length) {

  // Ignore empty MQTT messages
  if (length == 0) return;

  StaticJsonDocument<512> doc;

  // Parse JSON payload
  DeserializationError err = deserializeJson(doc, payload, length);

  // Abort on JSON parsing failure
  if (err) {
    Serial.print("JSON parse failed: ");
    Serial.println(err.c_str());
    return;
  }

  // ---------------------------------------------------------------------------
  // RFID access response: enable or disable keypad
  // ---------------------------------------------------------------------------
  if (strcmp(topic, net.makeTopic("access/response").c_str()) == 0) {

    // Enable keypad only if RFID access was granted
    kpEnabled = (doc["response"]["hasAccess"] | false)
      ? true
      : false;
  }
  // ---------------------------------------------------------------------------
  // PIN verification response: always disable keypad afterward
  // ---------------------------------------------------------------------------
  else if (strcmp(topic, net.makeTopic("access/keypad_response").c_str()) == 0) {

    // Prevent further input until next RFID authorization
    kpEnabled = false;
  }
}

// -----------------------------------------------------------------------------
// Setup
// -----------------------------------------------------------------------------

/**
 * @brief Arduino setup function.
 *
 * Initializes Serial output, WiFi connection,
 * MQTT client, and topic subscriptions.
 */
void setup() {
  Serial.begin(115200);

  net.begin(
    WIFI_SSID,
    WIFI_PASS,
    MQTT_HOST,
    MQTT_PORT,
    MQTT_USER,
    MQTT_PASS,
    DEVICE_ID,
    "site1"
  );

  Serial.println("Keypad + MQTT ready");

  net.setCallback(callback);

  // Subscribe to access control topics
  Serial.printf("access/response MQTT subscribe %s\n",
    net.subscribe(net.makeTopic("access/response").c_str()) ? "OK" : "FAILED");

  Serial.printf("access/keypad_response MQTT subscribe %s\n",
    net.subscribe(net.makeTopic("access/keypad_response").c_str()) ? "OK" : "FAILED");
}

// -----------------------------------------------------------------------------
// Helper functions
// -----------------------------------------------------------------------------

/**
 * @brief Resets the PIN input buffer and index.
 *
 * Clears any partially entered PIN and prepares
 * the buffer for new input.
 */
void reset_code() {
  memset(input, 0, sizeof(input));  // Clear buffer contents
  currentIdx = 0;                   // Reset write index
}

/**
 * @brief Publishes keypad tap feedback via MQTT.
 *
 * Sends the current PIN length after each valid keypress,
 * allowing external systems to provide visual or audio feedback.
 */
void publishTap() {
  StaticJsonDocument<64> data;
  data["event"]     = "KP_tap";
  data["pinlength"] = currentIdx;

  net.publishJson("keypad/tap", data);
}

// -----------------------------------------------------------------------------
// Main loop
// -----------------------------------------------------------------------------

/**
 * @brief Arduino main loop.
 *
 * Handles:
 * - MQTT client processing
 * - Keypad scanning
 * - PIN entry logic
 * - PIN submission and reset
 */
void loop() {
  net.loop();   // Process MQTT traffic
  yield();      // Allow background WiFi tasks

  // Read keypad state (non-blocking)
  char key = keypad.getKey();

  // Ignore input if no key pressed or keypad is disabled
  if (key == NO_KEY || !kpEnabled) {
    return;
  }

  // ---------------------------------------------------------------------------
  // Numeric key input
  // ---------------------------------------------------------------------------
  if (key >= '0' && key <= '9') {

    // Only accept input if buffer is not full
    if (currentIdx < CODE_LENGTH) {
      input[currentIdx++] = key;   // Store digit
      input[currentIdx] = '\0';    // Maintain null-terminated string

      Serial.print("Key: ");
      Serial.println(key);
    }
  }

  // ---------------------------------------------------------------------------
  // Submit PIN using '#'
  // ---------------------------------------------------------------------------
  else if (key == '#') {

    // Only submit if required PIN length is reached
    if (currentIdx == CODE_LENGTH) {
      Serial.print("Submitting code: ");
      Serial.println(input);

      // Build JSON payload for PIN submission
      StaticJsonDocument<64> data;
      data["event"] = "KP_try";
      data["code"]  = input;

      net.publishJson("keypad/submit", data);
    } 
    else {
      Serial.println("Code too short, resetting.");
    }

    // Always reset buffer after submission attempt
    reset_code();
  }

  // ---------------------------------------------------------------------------
  // Clear input using '*'
  // ---------------------------------------------------------------------------
  else if (key == '*') {
    reset_code();
    Serial.println("Input cleared");
  }

  // Ignore unsupported keys (A-D)
  else {
    return;
  }

  // Any valid keypress is treated as a tap event
  publishTap();
}
