// -------- Imported libraries --------
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <MFRC522.h>
#include <esp_system.h>

// -------- Wi-Fi & MQTT configuration --------
constexpr char WIFI_SSID[] = "PET vogn 11";
constexpr char WIFI_PASS[] = "vandmelon";

constexpr char MQTT_HOST[] = "maqiatto.com";
constexpr uint16_t MQTT_PORT = 1883;
constexpr char MQTT_USER[] = "hectorfoss@gmail.com";
constexpr char MQTT_PASS[] = "potter";

constexpr char DEVICE_ID[] = "door1";

// -------- Hardware pins --------
constexpr uint8_t PIN_SS = 21;   // RC522 SDA -> GPI21
constexpr uint8_t PIN_RST = 22; // RC522 RST -> GPIO22

// -------- Global state --------
MFRC522 rfid(PIN_SS, PIN_RST);

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

String baseTopic;

String topicOf(const String &suffix) {
  return baseTopic + "/" + suffix;
}

// Establish or re-establish Wi-Fi connectivity to provide network access for MQTT
void wifiConnect() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  Serial.printf("Connecting to WiFi SSID %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(250);
  }

  Serial.println();
  Serial.printf("WiFi connected to %s. IP: %s\n", WIFI_SSID, WiFi.localIP().toString().c_str());
}

// Send a UID to Node-RED when a card is detected
void publishEvent(const String &uid) {
  StaticJsonDocument<256> doc;
  doc["device_id"] = DEVICE_ID;
  doc["uid"] = uid;
  doc["ts"] = static_cast<uint32_t>(millis() / 1000);

  char out[256];
  size_t n = serializeJson(doc, out, sizeof(out));
  mqtt.publish(topicOf("access/request").c_str(), out, n);
}

// Keep trying to establish MQTT
void mqttConnect() {
  while (!mqtt.connected()) {
    String clientId = String("esp32_") + DEVICE_ID + "_" + String(static_cast<uint32_t>(ESP.getEfuseMac()), HEX);
    Serial.printf("Connecting to MQTT %s:%u as %s\n", MQTT_HOST, MQTT_PORT, clientId.c_str());

    if (mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
      Serial.printf("MQTT connected to %s:%u as %s\n", MQTT_HOST, MQTT_PORT, clientId.c_str());
    } else {
      Serial.printf("MQTT connect failed, state=%d. Retrying in 2s\n", mqtt.state());
      delay(2000);
    }
  }
}

// Read a single card UID, already debounced by PICC APIs
String readUidOnce() {
  if (!rfid.PICC_IsNewCardPresent()) {
    return "";
  }
  if (!rfid.PICC_ReadCardSerial()) {
    return "";
  }

  String uid;
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) {
      uid += "0";
    }
    uid += String(rfid.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  Serial.println(uid);
  return uid;
}

// -------- Setup --------
void setup() {
  Serial.begin(115200);

  baseTopic = String(MQTT_USER) + "/site1/" + DEVICE_ID;

  wifiConnect();

  SPI.begin();
  rfid.PCD_Init();
  Serial.println("RFID reader ready");

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(1024);

  mqttConnect();
  Serial.println("MQTT ready");
}

// -------- Main loop --------
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnect();
  }
  if (!mqtt.connected()) {
    mqttConnect();
  }
  mqtt.loop();

  String uid = readUidOnce();
  if (!uid.isEmpty()) {
    publishEvent(uid);
    delay(250);
  }
}
