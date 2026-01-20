/**
 * @file esp1_rfid_motion.ino
 * @brief RFID + PIN based door access controller with LCD, motion sensing, and MQTT backend.
 *
 * This firmware runs on an ESP-based Arduino-compatible board. It integrates:
 * - MFRC522 RFID reader (RFID-RC522)
 * - I2C LCD display
 * - PIR motion sensor
 * - MQTT communication over WiFi
 *
 * The system performs RFID authentication followed by PIN verification,
 * displaying status messages on an LCD and managing display backlight
 * based on motion activity.
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

/** @brief Duration (ms) to keep LCD backlight on after motion. */
constexpr uint32_t DISPLAY_BACKLIGHT_MS = 5000;

/** @brief Time window (ms) for entering PIN after RFID success. */
constexpr uint32_t PIN_TIME_MS = 15000;

/** @brief Door unlock display duration (ms). */
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

/** @brief Result of RFID authentication */
AccessResult rfidAccess;
/** @brief Result of PIN verification */
AccessResult accessGranted;

/** @brief Indicates whether a status message is currently displayed. */
bool textshown = false;

/** @brief Timestamp (ms) when the displayed text should expire. */
uint32_t showTextUntil = 0;

/** @brief Timestamp (ms) when LCD backlight should turn off. */
uint32_t showDisplayUntil = 0;

/** @brief Indicates whether motion is currently considered active. */
bool motionActive = false;

/** @brief Masked PIN buffer shown on LCD (max 4 digits). */
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
  lcd.print("                ");
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
  lcd.print("                ");
  lcd.setCursor(0, line);
  lcd.print(msg);
}

/**
 * @brief Forces the system back into locked idle state.
 *
 * Clears status flags, resets access state,
 * and restores default LCD message.
 */
static void forceLock() {
  textshown = false;
  rfidAccess = AccessResult::Denied;
  accessGranted = AccessResult::Denied;
  lcdPrintLine(F("Scan RFID card"), 0);
}

/**
 * @brief Builds a masked PIN string for LCD display.
 *
 * @param pinLength Number of digits entered so far.
 */
void makeEnteredPins(uint8_t pinLength)
{
    if (pinLength > 4) {
        pinLength = 4;
    }

    memset(enteredPins, '*', pinLength);
    memset(enteredPins + pinLength, ' ', 4 - pinLength);
    enteredPins[4] = '\0';
}

/**
 * @brief MQTT message callback handler.
 *
 * Handles:
 * - RFID access responses
 * - Keypad PIN verification responses
 * - Keypad visual feedback events
 *
 * @param topic MQTT topic string
 * @param payload Raw payload bytes
 * @param length Payload length
 */
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
  else if (strcmp(topic, net.makeTopic("access/keypad_response").c_str()) == 0) {
    if (rfidAccess != AccessResult::Granted) return;

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

    uint8_t pinLength = doc["data"]["pinlength"] | 0;
    makeEnteredPins(pinLength);
    lcdPrintLine(enteredPins, 1);
  }
}

/**
 * @brief Checks whether the LCD backlight should remain active.
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
 * @param now Current time in milliseconds.
 */
void onMotionDetected(uint32_t now) {
  if (!motionActive) {
    motionActive = true;
    Serial.println(F("Motion detected"));
    lcd.backlight();
  }

  showDisplayUntil = now + DISPLAY_BACKLIGHT_MS;
}

/**
 * @brief Handles motion-idle state.
 *
 * @param now Current time in milliseconds.
 */
void onMotionIdle(uint32_t now) {
  if (motionActive && !isDisplayActive(now)) {
    motionActive = false;
    lcd.noBacklight();
  }
}

/**
 * @brief Updates motion state based on PIR sensor input.
 *
 * @param now Current time in milliseconds.
 */
void updateMotionState(uint32_t now) {
  const bool motion = digitalRead(MOTION_PIN);

  if (motion) {
    onMotionDetected(now);
  } else {
    onMotionIdle(now);
  }
}

/**
 * @brief Converts an RFID UID to a hexadecimal string.
 *
 * @param uid MFRC522 UID structure.
 * @param output Destination buffer.
 * @param outputSize Size of destination buffer.
 */
void uidToHexString(const MFRC522::Uid& uid, char* output, size_t outputSize) {
  if (outputSize < (uid.size * 2 + 1)) {
    output[0] = '\0';
    return;
  }

  for (byte i = 0; i < uid.size; i++) {
    sprintf(&output[i * 2], "%02X", uid.uidByte[i]);
  }

  output[uid.size * 2] = '\0';
}

/**
 * @brief Handles RFID card detection and request publishing.
 *
 * Reads the UID, displays connection status,
 * and sends an MQTT access request.
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
 * Initializes hardware, peripherals, LCD,
 * WiFi connection, and MQTT subscriptions.
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
    DEVICE_ID,
    "site1"
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
 * - MQTT processing
 * - Display timeout logic
 * - Motion-based backlight control
 * - RFID card detection
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

  if (isDisplayActive(now))
    handleRFID();
}
