/**
 * @file esp1_rfid_motion.ino
 * @brief RFID + PIN based access controller with LCD, motion sensing, and MQTT.
 *
 * @defgroup esp1 ESP1 - RFID & Motion
 * @{
 *
 * @details
 * This firmware runs on an ESP-based Arduino-compatible board and implements
 * the first stage of a distributed door access control system.
 *
 * Hardware components:
 * - MFRC522 RFID reader (RC522)
 * - I2C LCD display
 * - PIR motion sensor
 *
 * Functional responsibilities:
 * - Detects motion to manage LCD backlight and user interaction
 * - Reads RFID tags and initiates authentication
 * - Displays system and authentication status on the LCD
 * - Communicates authentication requests and results via MQTT
 *
 * RFID authentication must succeed before the keypad stage is enabled.
 */




#include <Arduino.h>
#include <string.h>
#include <ArduinoJson.h>

#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

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

// -----------------------------------------------------------------------------
// Pin configuration | esp1&2.fzz
// -----------------------------------------------------------------------------

/** @brief RC522 SPI Slave Select pin (D8 / GPIO15). */
constexpr uint8_t RFID_SS_PIN  = 15;

/** @brief RC522 Reset pin (D0 / GPIO16). */
constexpr uint8_t RFID_RST_PIN = 16;

/** @brief Motion sensor pin (D3 / GPIO0). */
constexpr uint8_t MOTION_PIN = 0;

/** @brief I2C SDA pin for LCD (D2 / GPIO4). */
constexpr uint8_t I2C_SDA_PIN = 4;

/** @brief I2C SCL pin for LCD (D1 / GPIO5). */
constexpr uint8_t I2C_SCL_PIN = 5;

// -----------------------------------------------------------------------------
// LCD configuration
// -----------------------------------------------------------------------------

/** @brief Number of LCD columns. */
constexpr uint8_t LCD_COLUMNS = 16;

/** @brief Number of LCD rows. */
constexpr uint8_t LCD_ROWS    = 2;

/** @brief I2C address of the LCD module. */
constexpr uint8_t LCD_ADDRESS = 0x27;

// -----------------------------------------------------------------------------
// Timing configuration
// -----------------------------------------------------------------------------

/** @brief Duration (ms) to display text before resetting. */
constexpr uint32_t DISPLAY_MS = 3000;

/** @brief Duration (ms) to keep LCD backlight on after motion. */
constexpr uint32_t DISPLAY_BACKLIGHT_MS = 5000;

/** @brief Time window (ms) for entering PIN after RFID success. */
constexpr uint32_t PIN_TIME_MS = 15000;

/** @brief Door unlock display duration (ms). */
constexpr uint32_t UNLOCK_TIME_MS = 5000;

// -----------------------------------------------------------------------------
// Global objects
// -----------------------------------------------------------------------------

/** @brief MFRC522 RFID reader instance. */
MFRC522 mfrc522(RFID_SS_PIN, RFID_RST_PIN);

/** @brief I2C LCD instance. */
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLUMNS, LCD_ROWS);

// -----------------------------------------------------------------------------
// Global state
// -----------------------------------------------------------------------------

/**
 * @brief Access result enumeration.
 */
enum class AccessResult : uint8_t {
  Denied,   /**< Access denied */
  Granted   /**< Access granted */
};

/** @brief Result of RFID authentication. */
AccessResult rfidAccess;

/** @brief Result of PIN verification. */
AccessResult accessGranted;

/** @brief Indicates whether a status message is currently displayed. */
bool textshown = false;

/** @brief Timestamp (ms) when the displayed text should expire. */
uint32_t showTextUntil = 0;

/** @brief Timestamp (ms) when LCD backlight should turn off. */
uint32_t showDisplayUntil = 0;

/** @brief Indicates whether motion is currently considered active. */
bool motionActive = false;

/**
 * @brief Masked PIN buffer shown on LCD (max 4 digits + null terminator).
 *
 * Filled with '*' for entered digits and spaces for remaining slots.
 */
static char enteredPins[5] = "    ";

// -----------------------------------------------------------------------------
// Helper functions
// -----------------------------------------------------------------------------

/**
 * @brief Clears an LCD line and prints a flash-resident string.
 *
 * @param msg Flash string (use F("...")).
 * @param line LCD row index.
 */
void lcdPrintLine(const __FlashStringHelper* msg, uint8_t line) {
  lcd.setCursor(0, line);
  lcd.print("                ");   // Overwrite entire row to remove old content
  lcd.setCursor(0, line);
  lcd.print(msg);
}

/**
 * @brief Clears an LCD line and prints a RAM-resident string.
 *
 * @param msg C-string to print.
 * @param line LCD row index.
 */
void lcdPrintLine(const char* msg, uint8_t line) {
  lcd.setCursor(0, line);
  lcd.print("                ");   // Clear row to avoid leftover characters
  lcd.setCursor(0, line);
  lcd.print(msg);
}

/**
 * @brief Forces the system back into locked idle state.
 *
 * Resets access state, clears temporary flags,
 * and restores the default idle LCD message.
 */
static void forceLock() {
  textshown = false;                     // Exit message display mode
  rfidAccess = AccessResult::Denied;     // Clear RFID authorization state
  accessGranted = AccessResult::Denied;  // Clear PIN authorization state
  lcdPrintLine(F("Scan RFID card"), 0);  // Restore idle prompt
}

/**
 * @brief Builds a masked PIN string for LCD display.
 *
 * Converts a numeric PIN length into a visual representation
 * using '*' characters and trailing spaces.
 *
 * @param pinLength Number of digits entered so far.
 */
void makeEnteredPins(uint8_t pinLength)
{
    // Clamp PIN length to display capacity (4 digits)
    if (pinLength > 4) {
        pinLength = 4;
    }

    // Fill entered portion with masking characters
    memset(enteredPins, '*', pinLength);

    // Fill remaining positions with spaces to clear old characters
    memset(enteredPins + pinLength, ' ', 4 - pinLength);

    // Ensure string is null-terminated
    enteredPins[4] = '\0';
}

/**
 * @brief MQTT message callback handler.
 *
 * Parses incoming JSON messages and dispatches logic based on topic:
 * - RFID access decision
 * - Keypad PIN validation result
 * - Keypad keypress feedback
 *
 * @param topic MQTT topic string.
 * @param payload Raw payload bytes.
 * @param length Payload length.
 */
void callback(char* topic, byte* payload, unsigned int length) {
  // Ignore empty MQTT messages
  if (length == 0) return;

  StaticJsonDocument<512> doc;

  // Deserialize JSON payload into document
  DeserializationError err = deserializeJson(doc, payload, length);

  // Abort if JSON parsing fails
  if (err) {
    Serial.print("JSON parse failed: ");
    Serial.println(err.c_str());
    return;
  }

  // ---------------------------------------------------------------------------
  // RFID access response
  // ---------------------------------------------------------------------------
  if (strcmp(topic, net.makeTopic("access/response").c_str()) == 0) {

    // Timestamp when request was sent (provided by backend)
    uint32_t requestMs = doc["sent_ts_ms"] | 0;

    // Compute round-trip delay to detect stale responses
    uint32_t deltaMs = millis() - requestMs;

    Serial.printf(
      "Request/Response time diff: %lu ms\n",
      deltaMs
    );

    // Ignore responses that took too long to arrive
    if (deltaMs > DISPLAY_MS) {
      Serial.println("Took too long to respond");
      return;
    }

    // Extract access decision from JSON payload
    rfidAccess = (doc["response"]["hasAccess"] | false)
      ? AccessResult::Granted
      : AccessResult::Denied;

    Serial.println("UID match: waiting for PIN...");

    // Handle denied RFID access immediately
    if (rfidAccess != AccessResult::Granted) {
      lcdPrintLine(F("Access Denied"), 0);
      textshown = true;
      showTextUntil = millis() + DISPLAY_MS;
      return;
    }

    // Prompt user to enter PIN after successful RFID
    lcdPrintLine(F("Enter PIN:"), 0);
    textshown = true;
    showTextUntil = millis() + PIN_TIME_MS;
    
  } 
  // ---------------------------------------------------------------------------
  // Keypad PIN verification response
  // ---------------------------------------------------------------------------
  else if (strcmp(topic, net.makeTopic("access/keypad_response").c_str()) == 0) {

    // Ignore PIN responses if RFID was not authorized
    if (rfidAccess != AccessResult::Granted) return;

    // Extract PIN validation result
    accessGranted = (doc["response"]["accessGranted"] | false)
      ? AccessResult::Granted
      : AccessResult::Denied;

    // Handle incorrect PIN
    if (accessGranted != AccessResult::Granted) {
      Serial.println("Access Denied");
      lcdPrintLine(F("Access Denied"), 0);
      textshown = true;
      showTextUntil = millis() + DISPLAY_MS;
      return;
    }

    // Handle successful authentication
    Serial.println("Access Granted");
    lcdPrintLine(F("Access Granted"), 0);
    textshown = true;
    showTextUntil = millis() + UNLOCK_TIME_MS;

  }
  // ---------------------------------------------------------------------------
  // Keypad visual feedback (keypress)
  // ---------------------------------------------------------------------------
  else if (strcmp(topic, net.makeTopic("keypad/beep").c_str()) == 0) {

    // Only show keypad feedback after successful RFID
    if (rfidAccess != AccessResult::Granted) return;

    // Number of digits entered so far
    uint8_t pinLength = doc["data"]["pinlength"] | 0;

    // Update masked PIN display
    makeEnteredPins(pinLength);
    lcdPrintLine(enteredPins, 1);
  }
}

/**
 * @brief Checks whether the LCD backlight should remain active.
 *
 * Uses signed time comparison to safely handle millis() wraparound.
 *
 * @param now Current time in milliseconds.
 * @return true if display is active, false otherwise.
 */
bool isDisplayActive(uint32_t now) {
  return (int32_t)(now - showDisplayUntil) < 0;
}

/**
 * @brief Handles motion-detected events.
 *
 * Activates LCD backlight and refreshes display timeout.
 *
 * @param now Current time in milliseconds.
 */
void onMotionDetected(uint32_t now) {
  if (!motionActive) {
    motionActive = true;       // Mark motion as active
    Serial.println(F("Motion detected"));
    lcd.backlight();           // Turn on LCD backlight
  }

  // Extend backlight timeout while motion persists
  showDisplayUntil = now + DISPLAY_BACKLIGHT_MS;
}

/**
 * @brief Handles motion-idle state.
 *
 * Turns off LCD backlight when motion has ceased
 * and the timeout has expired.
 *
 * @param now Current time in milliseconds.
 */
void onMotionIdle(uint32_t now) {
  if (motionActive && !isDisplayActive(now)) {
    motionActive = false;      // Mark motion as inactive
    lcd.noBacklight();         // Turn off LCD backlight
  }
}

/**
 * @brief Updates motion state based on PIR sensor input.
 *
 * Dispatches to motion-detected or motion-idle handlers.
 *
 * @param now Current time in milliseconds.
 */
void updateMotionState(uint32_t now) {
  const bool motion = digitalRead(MOTION_PIN);  // Read PIR sensor state

  if (motion) {
    onMotionDetected(now);
  } else {
    onMotionIdle(now);
  }
}

/**
 * @brief Converts an RFID UID to a hexadecimal string.
 *
 * Each UID byte is encoded as two uppercase hexadecimal characters.
 *
 * @param uid MFRC522 UID structure.
 * @param output Destination buffer.
 * @param outputSize Size of destination buffer.
 */
void uidToHexString(const MFRC522::Uid& uid, char* output, size_t outputSize) {

  // Ensure output buffer is large enough:
  // 2 hex characters per byte + null terminator
  if (outputSize < (uid.size * 2 + 1)) {
    output[0] = '\0';  // Fail safely with empty string
    return;
  }

  // Convert each UID byte into two hex characters
  for (byte i = 0; i < uid.size; i++) {
    sprintf(&output[i * 2], "%02X", uid.uidByte[i]);
  }

  // Explicitly terminate C-string
  output[uid.size * 2] = '\0';
}

/**
 * @brief Handles RFID card detection and request publishing.
 *
 * Detects new cards, reads UID, updates LCD,
 * and sends an MQTT access request.
 */
void handleRFID() {

  // Exit if no new card is present
  if (!mfrc522.PICC_IsNewCardPresent()) {
    return;
  }

  // Exit if card UID could not be read
  if (!mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  char uidString[21];
  uidToHexString(mfrc522.uid, uidString, sizeof(uidString));

  Serial.println(uidString);
  lcdPrintLine(F("Connecting..."), 0);

  textshown = true;
  showTextUntil = millis() + DISPLAY_MS;

  // Build JSON payload for access request
  StaticJsonDocument<64> data;
  data["uid"] = uidString;  
  data["event"] = "RFID_Try";

  // Publish access request via MQTT
  bool ok = net.publishJson("access/request", data);
  Serial.println(ok ? "MQTT publish OK" : "MQTT publish FAILED");

  // Properly halt card communication
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}

/**
 * @brief Arduino setup function.
 *
 * Initializes hardware peripherals, LCD, WiFi,
 * MQTT client, and topic subscriptions.
 */
void setup() {
  delay(100); // Allow hardware to stabilize
  Serial.begin(115200);

  rfidAccess = AccessResult::Denied;

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  lcd.init();
  lcd.noBacklight();
  lcd.clear();
  lcdPrintLine(F("Scan RFID card"), 0);

  SPI.begin();
  mfrc522.PCD_Init();

  Serial.println("RC522 initialized");

  pinMode(MOTION_PIN, INPUT);

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

  Serial.println("MQTT ready");

  net.setCallback(callback);

  // Subscribe to required MQTT topics
  Serial.printf("access/response MQTT subscribe %s\n", 
    net.subscribe(net.makeTopic("access/response").c_str()) ? "OK" : "FAILED");

  Serial.printf("access/keypad_response MQTT subscribe %s\n", 
    net.subscribe(net.makeTopic("access/keypad_response").c_str()) ? "OK" : "FAILED");
  
  Serial.printf("keypad/beep MQTT subscribe %s\n", 
    net.subscribe(net.makeTopic("keypad/beep").c_str()) ? "OK" : "FAILED");
}

/**
 * @brief Arduino main loop.
 *
 * Executes non-blocking system logic:
 * - MQTT client processing
 * - Display timeout handling
 * - Motion-based backlight control
 * - RFID polling when active
 */
void loop() {
  net.loop();     // Process MQTT traffic
  yield();        // Allow background WiFi tasks
  
  const uint32_t now = millis();

  // If a message is currently displayed, wait for timeout
  if (textshown) {
    if ((int32_t)(now - showTextUntil) >= 0) {
      forceLock();            // Reset system when timeout expires
    }
    return;
  }

  // Update LCD backlight based on motion sensor
  updateMotionState(now);

  // Only allow RFID scans when display is active
  if (isDisplayActive(now)) {
    handleRFID();
  }
}
