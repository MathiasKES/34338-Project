#include <Arduino.h>
#include <string.h>
#include <ArduinoJson.h>

#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#include <WiFiMqttClient.h>

// ---------------- Network configuration ----------------

WifiMqttClient net;

constexpr char WIFI_SSID[] = "Mathias2.4";
constexpr char WIFI_PASS[] = "mrbombasticcallmefantastic";

constexpr char MQTT_HOST[] = "maqiatto.com";
constexpr uint16_t MQTT_PORT = 1883;
constexpr char MQTT_USER[] = "hectorfoss@gmail.com";
constexpr char MQTT_PASS[] = "potter";

constexpr char DEVICE_ID[] = "door1";

// -----------------------------------------------------------------------------
// Pin configuration | esp1&2.fzz
// -----------------------------------------------------------------------------

/** @brief RC522 SPI Slave Select pin (D8 / GPIO15). */
constexpr uint8_t RFID_SS_PIN  = 15;

/** @brief RC522 Reset pin (D0 / GPIO16). */
constexpr uint8_t RFID_RST_PIN = 16;

/** @brief Motion sensor pin (D3 / GPIO0) */
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

/** @brief Duration (ms) to display backlight before resetting. */
constexpr uint32_t DISPLAY_BACKLIGHT_MS = 5000;

constexpr uint32_t PIN_TIME_MS = 15000;

constexpr uint32_t UNLOCK_TIME_MS = 5000;


/** @brief Main loop polling delay (ms). */
constexpr uint32_t POLL_MS    = 30;

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

AccessResult rfidAccess;
AccessResult accessGranted;

/** @brief Indicates whether a result is currently being displayed. */
bool textshown = false;

/** @brief Timestamp (ms) when the display should reset to idle text. */
uint32_t showTextUntil = 0;

/** @brief Timestamp (ms) when the display should reset to idle backlight. */
uint32_t showDisplayUntil = 0;

/** @brief Boolean to keep display active for a duration */
bool motionActive = false;

/** @brief Keep track of pin length from node-red */
static char enteredPins[5] = "    ";

// -----------------------------------------------------------------------------
// Helper functions
// -----------------------------------------------------------------------------

/**
 * @brief Clears LCD line 0 and prints a message.
 *
 * @param msg Flash-resident string to display (use F("...")).
 */

void lcdPrintLine(const __FlashStringHelper* msg, uint8_t line) {
  lcd.setCursor(0, line);
  lcd.print("                ");
  lcd.setCursor(0, line);
  lcd.print(msg);
}

// Overload to also use msg
void lcdPrintLine(const char* msg, uint8_t line) {
  lcd.setCursor(0, line);
  lcd.print("                ");
  lcd.setCursor(0, line);
  lcd.print(msg);
}

/**
 * @brief Resets the system to the idle state.
 *
 * Clears the LCD, and stops textshown mode.
 */
static void forceLock() {
  textshown = false;
  rfidAccess = AccessResult::Denied;
  accessGranted = AccessResult::Denied;
  lcdPrintLine(F("Scan RFID card"), 0);
}

/** @brief Parse pins as char[5] */
void makeEnteredPins(uint8_t pinLength)
{
    if (pinLength > 4) {
        pinLength = 4;
    }

    memset(enteredPins, '*', pinLength);
    memset(enteredPins + pinLength, ' ', 4 - pinLength);
    enteredPins[4] = '\0';
}

// Receive response from MQTT broker
void callback(char* topic, byte* payload, unsigned int length) {
  if (length == 0) return;

  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, payload, length);

  if (err) {
    Serial.print("JSON parse failed: ");
    Serial.println(err.c_str());
    return;
  }

  if (strcmp(topic, net.makeTopic("access/response").c_str()) == 0) {
    uint32_t requestMs = doc["sent_ts_ms"] | 0;
    uint32_t deltaMs = millis() - requestMs;

    Serial.printf(
      "Request/Response time diff: %lu ms\n",
      deltaMs
    );

    if (deltaMs > DISPLAY_MS) {
      Serial.println("Took too long to respond");
      return;
    }

    rfidAccess = (doc["response"]["hasAccess"] | false)
      ? AccessResult::Granted
      : AccessResult::Denied;

    Serial.println("UID match: waiting for PIN...");

    if (rfidAccess != AccessResult::Granted) {
      lcdPrintLine(F("Access Denied"), 0);
      textshown = true;
      showTextUntil = millis() + DISPLAY_MS;
      return;
    }

    lcdPrintLine(F("Enter PIN:"), 0);

    textshown = true;
    showTextUntil = millis() + PIN_TIME_MS;
    
  } 
  // Unlock door on correct keypad response
  else if (strcmp(topic, net.makeTopic("access/keypad_response").c_str()) == 0) {
    if (rfidAccess != AccessResult::Granted) return;
    // Check if it is an old message


    // Print on LCD
    accessGranted = (doc["response"]["accessGranted"] | false)
      ? AccessResult::Granted
      : AccessResult::Denied;

    if (accessGranted != AccessResult::Granted) {
      Serial.println("Access Denied");
      lcdPrintLine(F("Access Denied"), 0);
      textshown = true;
      showTextUntil = millis() + DISPLAY_MS;
      return;
    }

    Serial.println("Access Granted");
    lcdPrintLine(F("Access Granted"), 0);
    textshown = true;
    showTextUntil = millis() + UNLOCK_TIME_MS;

  }
  else if (strcmp(topic, net.makeTopic("keypad/beep").c_str()) == 0) {
    if (rfidAccess != AccessResult::Granted) return;

    // Visual feedback for keypad tap
    uint8_t pinLength = doc["data"]["pinlength"] | 0;

    // Update entered pins
    makeEnteredPins(pinLength);

    lcdPrintLine(enteredPins, 1);
  }
}

/**
 * @brief Checks if display is active
 * 
 * @param now Time (ms) to take difference against
 * @return true if display is active
 * @return false if display is inactive
 */
bool isDisplayActive(uint32_t now) {
  return (int32_t)(now - showDisplayUntil) < 0;
}

/**
 * @brief Handles motion detected event
 * 
 * @param now Time (ms) to take difference against
 */
void onMotionDetected(uint32_t now) {
  if (!motionActive) {
    motionActive = true;
    Serial.println(F("Motion detected"));
    lcd.backlight();
  }

  // Always refresh timeout while motion is present
  showDisplayUntil = now + DISPLAY_BACKLIGHT_MS;
}

/**
 * @brief Handles motion idle event
 * 
 * @param now Time (ms) to take difference against
 */
void onMotionIdle(uint32_t now) {
  if (motionActive && !isDisplayActive(now)) {
    motionActive = false;
    lcd.noBacklight();
  }
}

/**
 * @brief Updates motion state based on sensor input
 * 
 * @param now Time (ms) to take difference against
 */
void updateMotionState(uint32_t now) {
  const bool motion = digitalRead(MOTION_PIN);

  if (motion) {
    onMotionDetected(now);
  } else {
    onMotionIdle(now);
  }
}

void uidToHexString(const MFRC522::Uid& uid, char* output, size_t outputSize) {
  // Each byte needs 2 hex chars + 1 null terminator at the end
  if (outputSize < (uid.size * 2 + 1)) {
    output[0] = '\0';  // fail safely
    return;
  }

  for (byte i = 0; i < uid.size; i++) {
    sprintf(&output[i * 2], "%02X", uid.uidByte[i]);
  }

  output[uid.size * 2] = '\0';
}

/**
 * @brief Handles RFID card detection and processing.
 *
 * Reads the card UID, checks authorization, displays result,
 * and resets the card state.
 */
void handleRFID() {
  if (!mfrc522.PICC_IsNewCardPresent()) {
    return;
  }

  if (!mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  char uidString[21];
  uidToHexString(mfrc522.uid, uidString, sizeof(uidString));

  Serial.println(uidString);
  lcdPrintLine(F("Connecting..."), 0);

  textshown = true;
  showTextUntil = millis() + DISPLAY_MS;

  StaticJsonDocument<64> data;
  data["uid"] = uidString;
  data["event"] = "RFID_Try";

  bool ok = net.publishJson("access/request", data); 
  Serial.println(ok ? "MQTT publish OK" : "MQTT publish FAILED");

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}

/**
 * @brief Arduino setup function.
 *
 * Initializes Serial, I2C, LCD, SPI and RFID reader.
 */
void setup() {
  delay(100);
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
    DEVICE_ID, // door
    "site1" // site
  );

  Serial.println("MQTT ready");

  net.setCallback(callback);
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
 * Handles:
 * - Display timeout logic
 * - RFID card detection and reading
 * - UID verification
 * - LCD updates
 */
void loop() {
  net.loop();
  yield();
  
  const uint32_t now = millis();

  if (textshown) {
    if ((int32_t)(now - showTextUntil) >= 0) forceLock();
    delay(POLL_MS);
    return;
  }

  updateMotionState(now);

  // RFID only allowed while display is active
  if (isDisplayActive(now))
    handleRFID();

  // delay(POLL_MS);
}
