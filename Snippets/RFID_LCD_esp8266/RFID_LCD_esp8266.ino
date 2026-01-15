/**
 * @file RFID_LCD_esp8266.ino
 * @brief RFID-based access control system using ESP8266MOD, RC522, LCD, and servo.
 *
 * This sketch implements a simple access control system:
 * - Reads RFID card UIDs using an MFRC522 reader
 * - Compares against a predefined allowed UID
 * - Displays access status on a 16x2 I2C LCD
 * - Controls a servo to simulate door lock/unlock
 * 
 * Hardware:
 * - ESP8266 (NodeMCU 0.9)
 * - MFRC522 RFID reader (SPI)
 * - 16x2 LCD with I2C backpack
 * - SG90 servo
 *
 * Notes:
 * - Uses non-blocking timing with millis()
 * - Ignores card reads while textshown a result
 * - Intended for long-running stability (no dynamic String allocation)
 */

// -----------------------------------------------------------------------------
// Imported libraries
// -----------------------------------------------------------------------------
#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>

// -----------------------------------------------------------------------------
// Pin configuration
// -----------------------------------------------------------------------------

/** @brief RC522 SPI Slave Select pin (D8 / GPIO15). */
constexpr uint8_t RFID_SS_PIN  = 15;

/** @brief RC522 Reset pin (D0 / GPIO16). */
constexpr uint8_t RFID_RST_PIN = 16;

/** @brief I2C SDA pin for LCD (D2 / GPIO4). */
constexpr uint8_t I2C_SDA_PIN = 4;

/** @brief I2C SCL pin for LCD (D1 / GPIO5). */
constexpr uint8_t I2C_SCL_PIN = 5;

/** @brief Servo signal pin (D4 / GPIO2). */
constexpr uint8_t SERVO_PIN = 2;

/** @brief Motion sensor pin (D3 / GPIO0) */
constexpr uint8_t MOTION_PIN = 0;

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

/** @brief Duration (ms) to display access result before resetting. */
constexpr uint32_t DISPLAY_MS = 3000;

/** @brief Duration (ms) to display backlight before resetting. */
constexpr uint32_t DISPLAY_BACKLIGHT_MS = 5000;

/** @brief Main loop polling delay (ms). */
constexpr uint32_t POLL_MS    = 30;

// -----------------------------------------------------------------------------
// RFID configuration
// -----------------------------------------------------------------------------

/**
 * @brief Allowed RFID UID.
 *
 * Only cards matching this UID are granted access.
 */
constexpr uint8_t ALLOWED_UID[] = { 0xE3, 0x0C, 0x0F, 0xDA };

/** @brief Length of the allowed UID in bytes. */
constexpr uint8_t ALLOWED_UID_LEN = sizeof(ALLOWED_UID);

// -----------------------------------------------------------------------------
// Global objects
// -----------------------------------------------------------------------------

/** @brief MFRC522 RFID reader instance. */
MFRC522 mfrc522(RFID_SS_PIN, RFID_RST_PIN);

/** @brief I2C LCD instance. */
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLUMNS, LCD_ROWS);

/** @brief Servo instance controlling the lock mechanism. */
Servo myservo;

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

/** @brief Indicates whether a result is currently being displayed. */
bool textshown = false;

/** @brief Indicates whether the servo is currently in the open position. */
bool servoOpen  = false;

/** @brief Timestamp (ms) when the display should reset to idle text. */
uint32_t showTextUntil = 0;

/** @brief Timestamp (ms) when the display should reset to idle backlight. */
uint32_t showDisplayUntil = 0;

/** @brief Indicates whether a person is near using sensory input */
bool person_near = false;

/** @brief Boolean to keep display active for a duration */
bool motionActive = false;


// -----------------------------------------------------------------------------
// Helper functions
// -----------------------------------------------------------------------------

/**
 * @brief Clears LCD line 0 and prints a message.
 *
 * @param msg Flash-resident string to display (use F("...")).
 */
void lcdPrintLine0(const __FlashStringHelper* msg) {
  lcd.setCursor(0, 0);
  lcd.print("                ");
  lcd.setCursor(0, 0);
  lcd.print(msg);
}

/**
 * @brief Resets the system to the idle state.
 *
 * Clears the LCD, closes the servo if open, and stops textshown mode.
 */
static void resetIdle() {
  textshown = false;
  lcdPrintLine0(F("Scan RFID card"));
  if (servoOpen) {
    myservo.write(0);
    servoOpen = false;
  }
}

/**
 * @brief Prints the currently read RFID UID to the serial monitor in hex.
 */
static void printUIDHex() {
  Serial.print(F("UID: "));
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    uint8_t b = mfrc522.uid.uidByte[i];
    if (b < 0x10) Serial.print('0');
    Serial.print(b, HEX);
  }
  Serial.println();
}

/**
 * @brief Checks whether the currently read UID is authorized.
 *
 * @return AccessResult::Granted if UID matches allowed UID,
 *         AccessResult::Denied otherwise.
 */
static AccessResult checkUID() {
  if (mfrc522.uid.size != ALLOWED_UID_LEN) return AccessResult::Denied;
  return (memcmp(mfrc522.uid.uidByte, ALLOWED_UID, ALLOWED_UID_LEN) == 0)
         ? AccessResult::Granted
         : AccessResult::Denied;
}

/**
 * @brief Displays access result and actuates servo if granted.
 *
 * @param r Result of the access check.
 */
static void showResult(AccessResult r) {
  textshown = true;
  showTextUntil = millis() + DISPLAY_MS;

  if (r == AccessResult::Granted) {
    lcdPrintLine0(F("Access granted"));
    myservo.write(180);
    servoOpen = true;
  } else {
    lcdPrintLine0(F("Access denied"));
  }
}


bool isDisplayActive(uint32_t now) {
  return (int32_t)(now - showDisplayUntil) < 0;
}

void onMotionDetected(uint32_t now) {
  if (!motionActive) {
    motionActive = true;
    Serial.println(F("Motion detected"));
    lcd.backlight();
  }

  // Always refresh timeout while motion is present
  showDisplayUntil = now + DISPLAY_BACKLIGHT_MS;
}

void onMotionIdle(uint32_t now) {
  if (motionActive && !isDisplayActive(now)) {
    motionActive = false;
    lcd.noBacklight();
  }
}

void updateMotionState(uint32_t now) {
  const bool motion = digitalRead(MOTION_PIN);

  if (motion) {
    onMotionDetected(now);
  } else {
    onMotionIdle(now);
  }
}

void handleRFID() {
  if (!mfrc522.PICC_IsNewCardPresent()) {
    return;
  }

  if (!mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  printUIDHex();

  const AccessResult result = checkUID();
  showResult(result);

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}

/**
 * @brief Arduino setup function.
 *
 * Initializes Serial, I2C, LCD, SPI, RFID reader, and servo.
 */
void setup() {
  Serial.begin(9600);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  lcd.init();
  lcd.noBacklight();
  lcd.clear();
  lcdPrintLine0(F("Scan RFID card"));

  SPI.begin();
  mfrc522.PCD_Init();

  myservo.attach(SERVO_PIN);
  myservo.write(0);

  pinMode(MOTION_PIN, INPUT);

  Serial.println(F("RC522 initialized"));
}

/**
 * @brief Arduino main loop.
 *
 * Handles:
 * - Display timeout logic
 * - RFID card detection and reading
 * - UID verification
 * - Servo and LCD updates
 */
void loop() {
  const uint32_t now = millis();

  if (textshown) {
    if ((int32_t)(now - showTextUntil) >= 0) resetIdle();
    delay(POLL_MS);
    return;
  }

  updateMotionState(now);

  // RFID only allowed while display is active
  if (isDisplayActive(now))
    handleRFID();

  delay(POLL_MS);
}
