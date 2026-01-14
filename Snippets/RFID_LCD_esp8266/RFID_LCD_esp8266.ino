#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#define SS_PIN 15 // SDA, GPIO15, D8
#define RST_PIN 16 // RST, GPIO16, D0 

#define LCD_COLUMNS 16
#define LCD_ROWS 2
#define LCD_ADDRESS 0x27

MFRC522 mfrc522(SS_PIN, RST_PIN);
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLUMNS, LCD_ROWS);

void setup() {
  Serial.begin(9600);
  // while (!Serial);   // optional, helps on some boards

  // Init LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Scan RFID card");

  // Init SPI + RFID
  SPI.begin();
  mfrc522.PCD_Init();

  Serial.println("RC522 initialized");
}

void loop() {
  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial()) return;

  // Clear second line
  lcd.setCursor(0, 1);
  lcd.print("                ");

  Serial.print("UID: ");

  // Print UID
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) {
      Serial.print("0");
      lcd.print("0");
    }
    Serial.print(mfrc522.uid.uidByte[i], HEX);
    Serial.print(" ");
    lcd.print(mfrc522.uid.uidByte[i], HEX);
  }
  Serial.println();

  // -------- UID CHECK --------
  bool accessGranted = false;

  if (mfrc522.uid.size == 4 &&
      mfrc522.uid.uidByte[0] == 0xE3 &&
      mfrc522.uid.uidByte[1] == 0x0C &&
      mfrc522.uid.uidByte[2] == 0x0F &&
      mfrc522.uid.uidByte[3] == 0xDA) {
    accessGranted = true;
  }

  // Show result
  lcd.setCursor(0, 0);
  if (accessGranted) {
    lcd.print("Access granted ");
  } else {
    lcd.print("Access denied  ");
  }

  // Halt card and stop encryption
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();

  delay(1000);
}

