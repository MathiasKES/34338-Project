#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd{0x27, 16, 2};

/*
 * @brief Arduino setup function
 */
void setup() {
  Serial.begin(9600);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0,0);
  lcd.print("  Hello world!  ");
}


/*
 * @brief Main Arduino loop
 */
void loop() {

  delay(500);
}
