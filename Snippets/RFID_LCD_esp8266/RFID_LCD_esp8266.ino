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

#define SERVO_PIN 0 // GPIO0, D3

MFRC522 mfrc522(SDA_pin, RST_PIN);
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLUMNS, LCD_ROWS);
Servo myservo;

// Global variables:
bool accessGranted;



///////////////////////////// Function /////////////////////////////


/* Function: 
  display on LCD 'Access granted'/'Access denied' 
  based on state of global variable accessGranted
*/
void displayResultOnLCD() {
  lcd.setCursor(0, 1);
  lcd.print("                ");

  lcd.setCursor(0, 0);
  if (accessGranted) {
    accessGranted = false;
    turning = true;
    lcd.print("Access granted  ");
  } 
  else lcd.print("Access denied  ");

  displaying = true;
}

/* Function: takes a UID as argument and print to Serial Monitor*/
void printUID(String UID) {
  UID.toUpperCase();
  Serial.println("UID: " + UID);
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
  accessGranted = false;

  if (mfrc522.uid.size == 4 && mfrc522.uid.uidByte[0] == 0xE3 && mfrc522.uid.uidByte[1] == 0x0C && mfrc522.uid.uidByte[2] == 0x0F && mfrc522.uid.uidByte[3] == 0xDA) {
    accessGranted = true;
  }
}

///////////////////////////// Setup /////////////////////////////
void setup() {
  Serial.begin(9600);

  // Init LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Scan RFID card");

  // Init SPI = (SCK MOSI MISO) + RFID
  SPI.begin();
  mfrc522.PCD_Init();

  myservo.attach(SERVO_PIN);
  myservo.write(0);

  Serial.println("RC522 initialized");
}

bool accessGranted = false;
bool turning = false;
uint16_t timer = 0;
bool displaying = false;


///////////////////////////// Loop /////////////////////////////
void loop() {
  if (displaying) {
    timer++;
    if (timer >= 120) {
      displaying = false;
      timer = 0;
      lcd.setCursor(0, 0);
      lcd.print("                ");
    }
  }

  if (turning) {
    Serial.println(myservo.read());
    if (myservo.read() < 180) {
      myservo.write(180);
    } else {
      myservo.write(0);

      // Reset
      turning = false;
      lcd.setCursor(0, 0);
      lcd.print("Scan RFID card  ");
    }
  }

  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial()) return;

  // Fetch card UID and print
  String myUID = getUID();
  printUID(myUID);


  checkCardUID();
  displayResultOnLCD();


  // Halt card and stop encryption
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();

  // delay(1000);
}
