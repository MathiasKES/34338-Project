/**
 * @file esp3.ino
 * @brief Door actuator, buzzer, and LED controller with MQTT integration.
 * @defgroup esp3 ESP3 - Door Actuator
 * @ingroup esp3
 *
 * @details
 * This firmware runs on an ESP-based Arduino-compatible board and implements
 * the final actuator stage of the access control system.
 *
 * Hardware components:
 * - Servo motor for door locking mechanism
 * - Status LEDs (red / green)
 * - Piezo buzzer
 *
 * Functional responsibilities:
 * - Locks and unlocks the door based on MQTT access decisions
 * - Provides audible and visual feedback for access events
 * - Automatically relocks the door after a configurable timeout
 * - Supports an administrative servo override mode via MQTT
 *
 * This node does not make access decisions itself; it only reacts
 * to authenticated results produced by the RFID and keypad nodes.
 *
 * @section esp3_functions Main functions
 * - setup() - Hardware, WiFi, and MQTT initialization
 * - loop()  - Actuator control, timeout handling, and buzzer state machine
 *
 * @section esp3_globals Global state
 * - Access state and unlock timing
 * - Servo position and override flags
 * - Buzzer pattern state machine variables
 *
 * @section esp3_source Source code
 * The full implementation is shown below.
 */

#include <Servo.h>

#include <ArduinoJson.h>
#include <WiFiMqttClient.h>

// ---------------- Network configuration ----------------

/** @brief WiFi + MQTT client wrapper */
WifiMqttClient net;

/** @brief WiFi SSID */
constexpr char WIFI_SSID[] = "Mathias2.4";
/** @brief WiFi password */
constexpr char WIFI_PASS[] = "mrbombasticcallmefantastic";

/** @brief MQTT broker hostname */
constexpr char MQTT_HOST[] = "maqiatto.com";
/** @brief MQTT broker port */
constexpr uint16_t MQTT_PORT = 1883;
/** @brief MQTT username */
constexpr char MQTT_USER[] = "hectorfoss@gmail.com";
/** @brief MQTT password */
constexpr char MQTT_PASS[] = "potter";

/** @brief Unique device identifier used in MQTT topics */
constexpr char DEVICE_ID[] = "door1";

// -----------------------------------------------------------------------------
// Pin configuration | esp3.fzz
// -----------------------------------------------------------------------------

/**
 * @brief GPIO pin assignments.
 *
 * Refer to esp3.fzz for wiring details.
 */
constexpr uint8_t RED_PIN    = 4;   /**< Red LED (locked / denied) */
constexpr uint8_t GREEN_PIN  = 0;   /**< Green LED (unlocked) */
constexpr uint8_t BUZZER_PIN = 14;  /**< Piezo buzzer */
constexpr uint8_t SERVO_PIN  = 13;  /**< Servo signal pin */

/** @brief Analog pin for potentiometer (admin servo control) */
#define POT_PIN A0

// -----------------------------------------------------------------------------
// Timing configuration
// -----------------------------------------------------------------------------

/** @brief Door unlock duration (ms). */
constexpr uint32_t UNLOCK_TIME_MS = 5000;

/** @brief Timestamp (ms) until which the door remains unlocked. */
uint32_t unlockUntil = 0;

// -----------------------------------------------------------------------------
// Global state
// -----------------------------------------------------------------------------

/** @brief Indicates whether the door is currently unlocked. */
bool unlocked = false;

/**
 * @brief Access result enumeration.
 */
enum class AccessResult : uint8_t {
  Denied,   /**< Access denied */
  Granted   /**< Access granted */
};

/** @brief Result of keypad PIN verification. */
AccessResult accessGranted;

/** @brief Indicates whether RFID access was granted. */
bool rfidAccess = false;

/** @brief Servo instance controlling the lock mechanism. */
Servo lock_servo;

/** @brief Indicates whether the servo is currently in the open position. */
bool servoOpen = false;

/** @brief Current servo angle (derived from potentiometer in admin mode). */
uint8_t servoAngle;

/** @brief Enables direct servo control via potentiometer (admin mode). */
bool adminServoControl = false;

// -----------------------------------------------------------------------------
// Buzzer state machine
// -----------------------------------------------------------------------------

/*
 * Timing arrays define alternating ON/OFF durations in milliseconds.
 * Even indices (LEFT): buzzer ON durations
 * Odd indices (RIGHT):  buzzer OFF durations
 */

/** @brief Denied access: single long beep. Extra long to scare */
const unsigned int deniedTimings[] = {
  2000
};

/** @brief Lock sound: long-long-long pattern. */
const unsigned int lockTimings[] = {
  250, 120,
  250, 120,
  250
};

/** @brief Unlock sound: short-short pattern. */
const unsigned int unlockTimings[] = {
  125, 120,
  125
};

/** @brief Keypad tap sound: short beep. */
const unsigned int beepTimings[] = {
  125
};

/**
 * @brief Buzzer state enumeration.
 */
enum BuzzerState {
  BUZZER_IDLE, /**< No sound playing */
  BUZZER_ON,   /**< Buzzer currently active */
  BUZZER_OFF   /**< Silent gap between beeps */
};

/**
 * @brief Describes a buzzer sound pattern.
 */
struct BuzzerPattern {
  const unsigned int* timings; /**< Pointer to timing array */
  uint8_t length;              /**< Number of timing entries */
};

/** @brief Current buzzer state. */
BuzzerState buzzerState = BUZZER_IDLE;

/** @brief Currently active buzzer pattern. */
BuzzerPattern currentPattern = {nullptr, 0};

/** @brief Index into the timing array. */
uint8_t stepIndex = 0;

/** @brief Timestamp of last buzzer state change. */
unsigned long lastChange = 0;

/**
 * @brief Starts playing a buzzer pattern.
 *
 * @param timings Pointer to timing array.
 * @param length Number of elements in the timing array.
 */
void playPattern(const unsigned int* timings, uint8_t length) {

  // Prevent overlapping sound patterns
  if (buzzerState != BUZZER_IDLE) return;

  currentPattern.timings = timings;
  currentPattern.length  = length;

  stepIndex   = 0;
  buzzerState = BUZZER_ON;
  lastChange  = millis();

  digitalWrite(BUZZER_PIN, HIGH);  // Start with buzzer ON
}

/**
 * @brief Advances the buzzer state machine.
 *
 * Must be called frequently from the main loop.
 */
void updateBuzzer() {
  if (buzzerState == BUZZER_IDLE) return;

  unsigned long now = millis();

  // Wait until current timing interval expires
  if (now - lastChange < currentPattern.timings[stepIndex]) return;

  lastChange = now;
  stepIndex++;

  // End of pattern reached
  if (stepIndex >= currentPattern.length) {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerState = BUZZER_IDLE;
    return;
  }

  // Toggle buzzer state
  if (buzzerState == BUZZER_ON) {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerState = BUZZER_OFF;
  } else {
    digitalWrite(BUZZER_PIN, HIGH);
    buzzerState = BUZZER_ON;
  }
}

/**
 * @brief Immediately stops any active buzzer sound.
 */
void stopBuzzer() {
  buzzerState = BUZZER_IDLE;
  digitalWrite(BUZZER_PIN, LOW);
}

/** @brief Plays lock sound pattern. */
void playLockSound() {
  stopBuzzer();
  playPattern(lockTimings,
              sizeof(lockTimings) / sizeof(lockTimings[0]));
}

/** @brief Plays unlock sound pattern. */
void playUnlockSound() {
  stopBuzzer();
  playPattern(unlockTimings,
              sizeof(unlockTimings) / sizeof(unlockTimings[0]));
}

/** @brief Plays denied-access sound pattern. */
void playDeniedSound() {
  stopBuzzer();
  playPattern(deniedTimings,
              sizeof(deniedTimings) / sizeof(deniedTimings[0]));
}

/** @brief Plays keypad tap sound. */
void playTapSound() {
  stopBuzzer();
  playPattern(beepTimings,
              sizeof(beepTimings) / sizeof(beepTimings[0]));
}

// -----------------------------------------------------------------------------
// Helper functions
// -----------------------------------------------------------------------------

/**
 * @brief Forces the system into a locked idle state.
 *
 * - Resets access state
 * - Sets LEDs to "locked"
 * - Moves servo to closed position if needed
 */
static void forceLock() {
  accessGranted = AccessResult::Denied;
  unlocked = false;

  digitalWrite(RED_PIN, HIGH);
  digitalWrite(GREEN_PIN, LOW);

  if (servoOpen) {
    lock_servo.write(0);   // Move servo to locked position
    servoOpen = false;
  }
}

// -----------------------------------------------------------------------------
// MQTT callback
// -----------------------------------------------------------------------------

/**
 * @brief MQTT message callback handler.
 *
 * Handles:
 * - Keypad access responses (unlock / deny)
 * - RFID access denials
 * - Keypad tap beep feedback
 * - Admin servo control enable/disable
 *
 * @param topic MQTT topic string.
 * @param payload Raw payload bytes.
 * @param length Payload length.
 */
void callback(char* topic, byte* payload, unsigned int length) {

  // Ignore empty MQTT messages
  if (length == 0) return;

  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, payload, length);

  // Abort if JSON parsing fails
  if (err) {
    Serial.print("JSON parse failed: ");
    Serial.println(err.c_str());
    return;
  }

  // ---------------------------------------------------------------------------
  // Keypad PIN verification response
  // ---------------------------------------------------------------------------
  if (strcmp(topic, net.makeTopic("access/keypad_response").c_str()) == 0) {

    // Ignore keypad responses during admin servo control
    if (adminServoControl) return;

    accessGranted = (doc["response"]["accessGranted"] | false)
      ? AccessResult::Granted
      : AccessResult::Denied;

    if (accessGranted != AccessResult::Granted) {
      Serial.println("Access Denied");
      playDeniedSound();
      forceLock();
      return;
    }

    // Access granted: unlock door
    Serial.println("Unlocking door");
    playUnlockSound();

    lock_servo.write(180);   // Move servo to unlocked position
    servoOpen = true;

    digitalWrite(GREEN_PIN, HIGH);
    digitalWrite(RED_PIN, LOW);

    unlocked = true;
    unlockUntil = millis() + UNLOCK_TIME_MS;
  }

  // ---------------------------------------------------------------------------
  // RFID access response (only react to denial)
  // ---------------------------------------------------------------------------
  else if (strcmp(topic, net.makeTopic("access/response").c_str()) == 0) {

    rfidAccess = (doc["response"]["hasAccess"] | false)
      ? true
      : false;

    if (!rfidAccess) {
      Serial.println("Access Denied");
      playDeniedSound();
      forceLock();
      return;
    }
  }

  // ---------------------------------------------------------------------------
  // Keypad tap feedback
  // ---------------------------------------------------------------------------
  else if (strcmp(topic, net.makeTopic("keypad/beep").c_str()) == 0) {

    // Only beep if RFID access is valid
    if (!rfidAccess) return;

    playTapSound();
  }

  // ---------------------------------------------------------------------------
  // Admin servo control enable/disable
  // ---------------------------------------------------------------------------
  else if (strcmp(topic, net.makeTopic("admin/servo_control").c_str()) == 0) {

    adminServoControl = (doc["data"]["adminServoControl"] | false)
      ? true
      : false;

    if (!adminServoControl) {
      Serial.println("Admin servo control disabled");
      forceLock();  // Reset servo to locked state
      return;
    }

    Serial.println("Admin servo control enabled");
  }
}

// -----------------------------------------------------------------------------
// Setup
// -----------------------------------------------------------------------------

/**
 * @brief Arduino setup function.
 *
 * Initializes GPIOs, servo, WiFi,
 * MQTT client, and topic subscriptions.
 */
void setup() {

  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  lock_servo.attach(SERVO_PIN);

  // Initialize outputs to safe state
  digitalWrite(RED_PIN, LOW);
  digitalWrite(GREEN_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  lock_servo.write(0);  // Locked position

  servoAngle = 0;

  delay(100);
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

  Serial.println("WiFi & MQTT ready");

  net.setCallback(callback);

  // Subscribe to required MQTT topics
  Serial.printf("access/response MQTT subscribe %s\n",
    net.subscribe(net.makeTopic("access/response").c_str()) ? "OK" : "FAILED");

  Serial.printf("access/keypad_response MQTT subscribe %s\n",
    net.subscribe(net.makeTopic("access/keypad_response").c_str()) ? "OK" : "FAILED");

  Serial.printf("keypad/beep MQTT subscribe %s\n",
    net.subscribe(net.makeTopic("keypad/beep").c_str()) ? "OK" : "FAILED");

  Serial.printf("admin/servo_control MQTT subscribe %s\n",
    net.subscribe(net.makeTopic("admin/servo_control").c_str()) ? "OK" : "FAILED");
}

// -----------------------------------------------------------------------------
// Main loop
// -----------------------------------------------------------------------------

/**
 * @brief Arduino main loop.
 *
 * Handles:
 * - MQTT processing
 * - Buzzer state machine updates
 * - Servo admin override mode
 * - Automatic relocking after timeout
 */
void loop() {
  net.loop();
  yield();

  // Advance buzzer state machine
  updateBuzzer();

  // ---------------------------------------------------------------------------
  // Admin servo control mode
  // ---------------------------------------------------------------------------
  if (adminServoControl) {

    // Map potentiometer value to servo angle
    servoAngle = (int)(analogRead(POT_PIN) / 1023.0f * 180.0f);
    servoAngle = constrain(servoAngle, 0, 180);

    lock_servo.write(servoAngle);
    return;
  }

  // ---------------------------------------------------------------------------
  // Automatic relock after unlock timeout
  // ---------------------------------------------------------------------------
  const uint32_t now = millis();

  if (unlocked && (int32_t)(now - unlockUntil) >= 0) {
    Serial.println("Locking door");
    playLockSound();
    forceLock();
  }
}
