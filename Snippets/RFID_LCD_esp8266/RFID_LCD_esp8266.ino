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
 * - Ignores card reads while displaying a result
 * - Intended for long-running stability (no dynamic String allocation)
 */

// Imported libraries:
#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>

// ---- Pins (match your current wiring) ----
// RC522 (SPI)
constexpr uint8_t RFID_SS_PIN  = 15; // D8 / GPIO15
constexpr uint8_t RFID_RST_PIN = 16; // D0 / GPIO16

// LCD (I2C) - NodeMCU defaults are SDA=D2(GPIO4), SCL=D1(GPIO5)
constexpr uint8_t I2C_SDA_PIN = 4;   // D2 / GPIO4
constexpr uint8_t I2C_SCL_PIN = 5;   // D1 / GPIO5

// Servo
constexpr uint8_t SERVO_PIN = 2;     // D4 / GPIO2

// LCD config
constexpr uint8_t LCD_COLUMNS = 16;
constexpr uint8_t LCD_ROWS    = 2;
constexpr uint8_t LCD_ADDRESS = 0x27;

// Timing
constexpr uint32_t DISPLAY_MS = 3000;
constexpr uint32_t POLL_MS    = 30;

// Allowed UID
constexpr uint8_t ALLOWED_UID[] = { 0xE3, 0x0C, 0x0F, 0xDA };
constexpr uint8_t ALLOWED_UID_LEN = sizeof(ALLOWED_UID);

MFRC522 mfrc522(RFID_SS_PIN, RFID_RST_PIN);
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLUMNS, LCD_ROWS);
Servo myservo;

enum class AccessResult : uint8_t { Denied, Granted };

bool displaying = false;
bool servoOpen  = false;
uint32_t displayUntil = 0;

///////////////////////////// Function /////////////////////////////
void lcdPrintLine0(const __FlashStringHelper* msg) {
  lcd.setCursor(0, 0);
  lcd.print("                ");
  lcd.setCursor(0, 0);
  lcd.print(msg);
}

static void resetIdle() {
  displaying = false;
  lcdPrintLine0(F("Scan RFID card"));
  if (servoOpen) {
    myservo.write(0);
    servoOpen = false;
  }
}

static void printUIDHex() {
  Serial.print(F("UID: "));
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    uint8_t b = mfrc522.uid.uidByte[i];
    if (b < 0x10) Serial.print('0');
    Serial.print(b, HEX);  // typically prints A-F uppercase
  }
  Serial.println();
}

static AccessResult checkUID() {
  if (mfrc522.uid.size != ALLOWED_UID_LEN) return AccessResult::Denied;
  return (memcmp(mfrc522.uid.uidByte, ALLOWED_UID, ALLOWED_UID_LEN) == 0)
         ? AccessResult::Granted
         : AccessResult::Denied;
}

static void showResult(AccessResult r) {
  displaying = true;
  displayUntil = millis() + DISPLAY_MS;

  if (r == AccessResult::Granted) {
    lcdPrintLine0(F("Access granted"));
    myservo.write(180);
    servoOpen = true;
  } else {
    lcdPrintLine0(F("Access denied"));
  }
}

///////////////////////////// Setup /////////////////////////////
void setup() {
  Serial.begin(9600);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcdPrintLine0(F("Scan RFID card"));

  SPI.begin();
  mfrc522.PCD_Init();

  myservo.attach(SERVO_PIN);
  myservo.write(0);

  Serial.println(F("RC522 initialized"));
}


///////////////////////////// Loop /////////////////////////////
void loop() {
  const uint32_t now = millis();

  if (displaying) {
    if ((int32_t)(now - displayUntil) >= 0) resetIdle();
    delay(POLL_MS);
    return;
  }

  if (!mfrc522.PICC_IsNewCardPresent()) {
    delay(POLL_MS);
    return;
  }

  if (!mfrc522.PICC_ReadCardSerial()) {
    delay(POLL_MS);
    return;
  }

  printUIDHex();

  const AccessResult result = checkUID();
  showResult(result);

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();

  delay(POLL_MS);
}
