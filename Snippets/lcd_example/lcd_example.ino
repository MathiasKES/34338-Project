#include <LiquidCrystal_I2C.h>

#define SS_PIN 10
#define RST_PIN 9
#define LCD_COLUMNS 16
#define LCD_ROWS 2
#define LCD_ADDRESS 0x27

LiquidCrystal_I2C lcd{LCD_ADDRESS, LCD_COLUMNS, LCD_ROWS};

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

uint64_t counter = 0;

/*
 * @brief Main Arduino loop
 */
void loop() {
  lcd_update(counter);

  counter++;
  if (counter > 9999999999999999ULL) counter = 0;
}


void lcd_update(uint64_t v)
{
  char buf[17] = "                "; // 16 spaces + '\0'

  int i = 15;
  do {
    buf[i--] = '0' + (v % 10);
    v /= 10;
  } while (v && i >= 0);

  lcd.setCursor(0, 1);
  lcd.print(buf);
}
