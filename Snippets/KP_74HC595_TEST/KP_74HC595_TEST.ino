#include <Arduino.h>
#include <string.h>

// ------------------ Keypad layout ------------------
const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

// ------------------ 74HC595 pins ------------------
constexpr uint8_t HC595_SHCP = D5;
constexpr uint8_t HC595_STCP = D1;
constexpr uint8_t HC595_DS   = D7;

// ------------------ Column pins ------------------
constexpr uint8_t colPins[COLS] = { D2, D3, D4, D6 };

// ------------------ Code logic ------------------
const char* code = "1234";
const uint8_t CODE_LENGTH = 4;

char input[CODE_LENGTH + 1] = {0};
uint8_t currentidx = 0;

// ------------------ Helpers ------------------
void hc595Write(uint8_t value) {
  digitalWrite(HC595_STCP, LOW);
  shiftOut(HC595_DS, HC595_SHCP, MSBFIRST, value);
  digitalWrite(HC595_STCP, HIGH);
}

char scanKeypad() {
  // Active-LOW rows: only one row LOW at a time
  static const uint8_t rowMask[ROWS] = {
    0b11111110, // Q0 low
    0b01111111, // Q7 low
    0b10111111, // Q6 low
    0b11011111  // Q5 low
  };

  for (uint8_t r = 0; r < ROWS; r++) {
    hc595Write(rowMask[r]);
    delayMicroseconds(5);

    for (uint8_t c = 0; c < COLS; c++) {
      if (digitalRead(colPins[c]) == LOW) {
        while (digitalRead(colPins[c]) == LOW); // wait for release
        return keys[r][c];
      }
    }
  }
  return 0;
}

// ------------------ Setup ------------------
void setup() {
  Serial.begin(9600);

  pinMode(HC595_SHCP, OUTPUT);
  pinMode(HC595_STCP, OUTPUT);
  pinMode(HC595_DS, OUTPUT);

  for (uint8_t i = 0; i < COLS; i++) {
    pinMode(colPins[i], INPUT_PULLUP);
  }

  hc595Write(0xFF); // all rows HIGH (idle)

  Serial.println(F("Keypad test ready"));
}

// ------------------ Loop ------------------
void loop() {
  char key = scanKeypad();

  if (key) {
    if (key >= '0' && key <= '9') {
      if (currentidx < CODE_LENGTH) {
        input[currentidx++] = key;
        input[currentidx] = '\0';
        Serial.println(key);
      }
    }
    else if (key == '#') {
      if (strcmp(input, code) == 0)
        Serial.println(F("Correct!"));
      else
        Serial.println(F("Incorrect!"));

      memset(input, 0, sizeof(input));
      currentidx = 0;
    }
    else if (key == '*') {
      memset(input, 0, sizeof(input));
      currentidx = 0;
      Serial.println(F("Cleared"));
    }
  }
}
