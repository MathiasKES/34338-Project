#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#define SS_PIN 10
#define RST_PIN 9

#define LCD_COLUMNS 16
#define LCD_ROWS 2
#define LCD_ADDRESS 0x27

MFRC522 mfrc522(SS_PIN, RST_PIN);
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLUMNS, LCD_ROWS);

void setup() {
  Serial.begin(9600);
  while (!Serial);   // optional, helps on some boards

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
  // Look for new card
  if (!mfrc522.PICC_IsNewCardPresent()) {
    return;
  }

  // Select the card
  if (!mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  // Clear LCD second line
  lcd.setCursor(0, 1);
  lcd.print("                ");

  // Print UID to Serial and LCD
  lcd.setCursor(0, 1);
  Serial.print("UID: ");

  for (byte i = 0; i < mfrc522.uid.size; i++) {
    // Serial output
    if (mfrc522.uid.uidByte[i] < 0x10)
      Serial.print("0");
    Serial.print(mfrc522.uid.uidByte[i], HEX);
    Serial.print(" ");

    // LCD output (hex, uppercase)
    if (mfrc522.uid.uidByte[i] < 0x10)
      lcd.print("0");
    lcd.print(mfrc522.uid.uidByte[i], HEX);
  }

  Serial.println();

  // Halt card and stop encryption
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();

  delay(1000); // avoid repeated reads
}
