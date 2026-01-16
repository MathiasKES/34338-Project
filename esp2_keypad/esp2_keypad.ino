#include <Arduino.h>
#include <Keypad.h>
#include <string.h>
#include <ArduinoJson.h>

#include <WiFiMqttClient.h>

// ---------------- Network configuration ----------------

WifiMqttClient net;

constexpr char WIFI_SSID[] = "Mathias iPhone";
constexpr char WIFI_PASS[] = "mrbombastic";

constexpr char MQTT_HOST[] = "maqiatto.com";
constexpr uint16_t MQTT_PORT = 1883;
constexpr char MQTT_USER[] = "hectorfoss@gmail.com";
constexpr char MQTT_PASS[] = "potter";

constexpr char DEVICE_ID[] = "door1";

// ---------------- Keypad configuration ----------------

const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

byte rowPins[ROWS] = {16, 5, 4, 0};
byte colPins[COLS] = {2, 14, 12, 13};

// R1 -> D8, R2 -> D7, R3 -> D6, R4 -> D5, C1 -> D4, C2 -> D3, C3 -> D2, C4 -> D1
// D1=5, D2=4, D3=0, D4=2, D5=14, D6=12, D7=13, D8=15

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ---------------- Code handling ----------------

constexpr uint8_t CODE_LENGTH = 4;

char input[CODE_LENGTH + 1] = {0};
uint8_t currentIdx = 0;

// ---------------- Setup ----------------

void setup() {
  Serial.begin(115200);

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

  Serial.println("Keypad + MQTT ready");
}

void reset_code() {
  memset(input, 0, sizeof(input));
  currentIdx = 0;
}

// ---------------- Main loop ----------------

void loop() {
  net.loop();

  char key = keypad.getKey();

  if (key == NO_KEY) {
    return;
  }

  // Numeric input
  if (key >= '0' && key <= '9') {
    if (currentIdx < CODE_LENGTH) {
      input[currentIdx++] = key;
      input[currentIdx] = '\0';
      Serial.print("Key: ");
      Serial.println(key);
    }
    return;
  }

  // Submit code
  if (key == '#') {
    if (currentIdx == CODE_LENGTH) {
      Serial.print("Submitting code: ");
      Serial.println(input);

      // BaseTopic: hectorfoss@gmail.com/site1/door1/
      StaticJsonDocument<64> data;
      data["code"] = input;
      data["event"] = "KP_try";

      net.publishJson("keypad/submit", data);
    } 
    else Serial.println("Code too short, resetting.");

    // Reset buffer
    reset_code();
    return;
  }

  // Clear input
  if (key == '*') {
    reset_code();
    Serial.println("Input cleared");
  }
}
