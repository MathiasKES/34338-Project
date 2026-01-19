#include <Servo.h>

#include <ArduinoJson.h>
#include <WiFiMqttClient.h>

// ---------------- Network configuration ----------------

WifiMqttClient net;

constexpr char WIFI_SSID[] = "Mathias2.4";
constexpr char WIFI_PASS[] = "mrbombasticcallmefantastic";

constexpr char MQTT_HOST[] = "maqiatto.com";
constexpr uint16_t MQTT_PORT = 1883;
constexpr char MQTT_USER[] = "hectorfoss@gmail.com";
constexpr char MQTT_PASS[] = "potter";

constexpr char DEVICE_ID[] = "door1";


// -----------------------------------------------------------------------------
// Pin configuration | esp3.fzz
// -----------------------------------------------------------------------------

/** @brief  */

constexpr uint8_t RED_PIN    = 4;
constexpr uint8_t GREEN_PIN  = 0;
constexpr uint8_t BUZZER_PIN = 14;
constexpr uint8_t SERVO_PIN  = 13;

// -----------------------------------------------------------------------------
// Timing configuration
// -----------------------------------------------------------------------------

/** @brief Unlock time (ms). */
constexpr uint32_t UNLOCK_TIME_MS    = 5000;
uint32_t unlockUntil = 0;

// -----------------------------------------------------------------------------
// Global state
// -----------------------------------------------------------------------------

bool unlocked = false;

/**
 * @brief Access result enumeration.
 */
enum class AccessResult : uint8_t {
  Denied,   /**< Access denied */
  Granted   /**< Access granted */
};

AccessResult accessGranted;

/** @brief Servo instance controlling the lock mechanism. */
Servo lock_servo;

/** @brief Indicates whether the servo is currently in the open position. */
bool servoOpen  = false;



// Buzzer state machine
/** @brief Lock: short-short-long */
// LEFT TIMINGS: beep-durations
// RIGHT TIMINGS: silence-durations
const unsigned int deniedTimings[] = {
  2000
};

const unsigned int lockTimings[] = {
  250, 120,
  250, 120,
  250
};

/** @brief Unlock: long-short-short (distinct but related) */
const unsigned int unlockTimings[] = {
  125, 120,
  125
};

enum BuzzerState {
  BUZZER_IDLE,
  BUZZER_ON,
  BUZZER_OFF
};

struct BuzzerPattern {
  const unsigned int* timings;
  uint8_t length;
};

BuzzerState buzzerState = BUZZER_IDLE;
BuzzerPattern currentPattern = {nullptr, 0};

uint8_t stepIndex = 0;
unsigned long lastChange = 0;

void playPattern(const unsigned int* timings, uint8_t length) {
  if (buzzerState != BUZZER_IDLE) return;  // prevent overlap

  currentPattern.timings = timings;
  currentPattern.length = length;

  stepIndex = 0;
  buzzerState = BUZZER_ON;
  lastChange = millis();

  digitalWrite(BUZZER_PIN, HIGH);
}

void updateBuzzer() {
  if (buzzerState == BUZZER_IDLE) return;

  unsigned long now = millis();
  if (now - lastChange < currentPattern.timings[stepIndex]) return;

  lastChange = now;
  stepIndex++;

  if (stepIndex >= currentPattern.length) {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerState = BUZZER_IDLE;
    return;
  }

  if (buzzerState == BUZZER_ON) {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerState = BUZZER_OFF;
  } else {
    digitalWrite(BUZZER_PIN, HIGH);
    buzzerState = BUZZER_ON;
  }
}

void stopBuzzer() {
  buzzerState = BUZZER_IDLE;
  digitalWrite(BUZZER_PIN, LOW);
}

void playLockSound() {
  stopBuzzer();
  playPattern(lockTimings,
              sizeof(lockTimings) / sizeof(lockTimings[0]));
}

void playUnlockSound() {
  stopBuzzer();
  playPattern(unlockTimings,
              sizeof(unlockTimings) / sizeof(unlockTimings[0]));
}

void playDeniedSound() {
  stopBuzzer();
  playPattern(deniedTimings,
              sizeof(deniedTimings) / sizeof(deniedTimings[0]));
}




// -----------------------------------------------------------------------------
// Helper functions
// -----------------------------------------------------------------------------

/**
 * @brief Resets the system to the idle state.
 *
 * Resets servo, sets LED to red and 
 */
static void forceLock() {
  accessGranted = AccessResult::Denied;
  unlocked = false;
  digitalWrite(RED_PIN, HIGH);
  digitalWrite(GREEN_PIN, LOW);
  if (servoOpen) {
    lock_servo.write(0);
    servoOpen = false;
  }
}


// Receive response from MQTT broker
void callback(char* topic, byte* payload, unsigned int length) {
  if (length == 0) return;

  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, payload, length);

  if (err) {
    Serial.print("JSON parse failed: ");
    Serial.println(err.c_str());
    return;
  }

  if (strcmp(topic, net.makeTopic("access/keypad_response").c_str()) == 0) {
    accessGranted = (doc["response"]["accessGranted"] | false)
      ? AccessResult::Granted
      : AccessResult::Denied;

      if (accessGranted != AccessResult::Granted) {
        Serial.println("Access Denied");
        playDeniedSound();
        forceLock();
        return;
      }

      // Access granted
      Serial.println("Unlocking door");
      playUnlockSound();
      lock_servo.write(180);
      servoOpen = true;
      digitalWrite(GREEN_PIN, HIGH);
      digitalWrite(RED_PIN, LOW);

      unlocked = true;
      unlockUntil = millis() + UNLOCK_TIME_MS;
  // RFID Response, only reacts to denied here
  } else if (strcmp(topic, net.makeTopic("access/response").c_str()) == 0) {
    bool hasAccess = (doc["response"]["hasAccess"] | false)
      ? true
      : false;

    if (!hasAccess) {
      Serial.println("Access Denied");
      playDeniedSound();
      forceLock();
      return;
    }
  }
}


void setup() {
  // put your setup code here, to run once:
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  lock_servo.attach(SERVO_PIN);

  digitalWrite(RED_PIN, LOW);
  digitalWrite(GREEN_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);  
  lock_servo.write(0);

  delay(100);
  Serial.begin(115200);

  net.begin(
    WIFI_SSID,
    WIFI_PASS,
    MQTT_HOST,
    MQTT_PORT,
    MQTT_USER,
    MQTT_PASS,
    DEVICE_ID, // door
    "site1" // site
  );

  Serial.println("WiFi & MQTT ready");

  net.setCallback(callback);
  Serial.printf("access/response MQTT subscribe %s\n", 
    net.subscribe(net.makeTopic("access/response").c_str()) ? "OK" : "FAILED");

  Serial.printf("access/keypad_response MQTT subscribe %s\n", 
    net.subscribe(net.makeTopic("access/keypad_response").c_str()) ? "OK" : "FAILED");
} 

/**
 * @brief Arduino main loop.
 *
 * Handles:
 * - Buzzer state machine
 * - Access granted/denied actions
 */
void loop() {
  net.loop();
  yield();

  updateBuzzer();

  const uint32_t now = millis();

  if (unlocked && (int32_t)(now - unlockUntil) >= 0) {
    Serial.println("Locking door");
    playLockSound();
    forceLock();
  }

}
