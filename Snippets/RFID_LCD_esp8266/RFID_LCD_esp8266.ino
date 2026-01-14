// Imported libraries:
#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>

// Global PIN:
#define SDA_pin 15  // SDA, GPIO15, D8
#define RST_PIN 16  // RST, GPIO16, D0
#define LCD_COLUMNS 16
#define LCD_ROWS 2
#define LCD_ADDRESS 0x27

#define SERVO_PIN 2 // GPIO2, D4

MFRC522 mfrc522(SDA_pin, RST_PIN);
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLUMNS, LCD_ROWS);
Servo myservo;

// Global variables:
uint8_t accessStatus; // 0: No card detected, 1: Denied card, 2: Accepted card
bool displaying;
bool turning;
uint16_t timer;
uint8_t pos;
bool new_card_present;
bool card_read;

///////////////////////////// Function /////////////////////////////

/* Function: 
  display on LCD 'Access granted'/'Access denied' 
  based on state of global variable accessStatus
*/
void displayResultOnLCD() {
  lcd.setCursor(0, 0);
  if (accessStatus == 2) {
    accessStatus = 0;
    displaying = true;
    myservo.write(180);

    turning = true;
    lcd.print("Access granted  ");
  } 
  else if (accessStatus == 1) {
    accessStatus = 0;
    displaying = true;

    lcd.print("Access denied   ");
  }
}

/* Function: takes a UID as argument and print to Serial Monitor*/
void printUID(String UID) {
  UID.toUpperCase();
  Serial.print("UID: " + UID + "\n");
}

/* Function: reads the UID from the card 
   and returns the UID in string HEX: xxxxxxxx*/
String getUID() {
  String registeredUID = "";

  // Loop over each byte of the UID
  for (byte i = 0; i < mfrc522.uid.size; i++) {

    // Leading 0 for e.g. 'A' -> '0A'
    if (mfrc522.uid.uidByte[i] < 0x10) {
      registeredUID += "0";
    }
    // convert byte to hex
    registeredUID += String(mfrc522.uid.uidByte[i], HEX);
  }

  return registeredUID;
}

/* Function: The logic that determines 
  when a USER get access or not
*/
void checkCardUID() {
  accessStatus = 1; // Access denied

  if (mfrc522.uid.size == 4 
      && mfrc522.uid.uidByte[0] == 0xE3 
      && mfrc522.uid.uidByte[1] == 0x0C 
      && mfrc522.uid.uidByte[2] == 0x0F 
      && mfrc522.uid.uidByte[3] == 0xDA) {
    accessStatus = 2; // Access granted
  }
}

///////////////////////////// Setup /////////////////////////////
void setup() {
  accessStatus = 0;
  displaying = false;
  turning = false;
  timer = 0;
  pos = 0;

  Serial.begin(9600);

  // Init LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Scan RFID card  ");

  // Init SPI = (SCK MOSI MISO) + RFID
  SPI.begin();
  mfrc522.PCD_Init();

  myservo.attach(SERVO_PIN);
  myservo.write(0);

  Serial.print("Rc522 initialized\n");
}


///////////////////////////// Loop /////////////////////////////
void loop() {
  if (displaying) {
    timer++;
    if (timer >= 100) { // 3000ms / 30ms = 100 ticks
      displaying = false;
      timer = 0;
      lcd.setCursor(0, 0);
      lcd.print("Scan RFID card  ");
      if (turning) {
        myservo.write(0);
        turning = false;
      }
    }
    return; // Ignore card reads/rest of code while displaying
  }

  ///////////////////////////// Operations that should happen after card detected - only happens once! /////////////////////////////
  new_card_present = mfrc522.PICC_IsNewCardPresent();
  card_read = mfrc522.PICC_ReadCardSerial();

  if (new_card_present && card_read) {
    // Fetch card UID and print
    String myUID = getUID();
    printUID(myUID);

    checkCardUID();
    displayResultOnLCD();


    // Halt card and stop encryption
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
  }

  delay(30);
}
