// -------- Imported libraries --------
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <SPI.h>
#include <MFRC522.h>
#include <FS.h>
#include <LittleFS.h>
#include <vector>
#include <algorithm>
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
Preferences prefs;

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

String baseTopic;

std::vector<String> allowlist;
uint32_t allowlistVersion = 0;

struct ChunkState {
  uint32_t version = 0;
  int total = 0;
  std::vector<String> uids;
  bool active = false;
} chunkState;

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

// Load locally cached UID allowlist from LittleFS
bool fileLoadAllowlist() {
  allowlist.clear();

  if (!LittleFS.exists("/allowlist.txt")) {
    return false;
  }

  File f = LittleFS.open("/allowlist.txt", "r");
  if (!f) {
    return false;
  }

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (!line.isEmpty()) {
      allowlist.push_back(line);
    }
  }
  f.close();

  std::sort(allowlist.begin(), allowlist.end());
  return true;
}

// Write the received allowlist to LittleFS so it survives reboots
bool fileWriteAllowlist(const std::vector<String> &uids) {
  File f = LittleFS.open("/allowlist.txt", "w");
  if (!f) {
    return false;
  }

  for (const auto &uid : uids) {
    f.println(uid);
  }
  f.close();
  return true;
}

// Fast binary-search membership check using the sorted allowlist
bool isAuthorizedLocal(const String &uid) {
  return std::binary_search(allowlist.begin(), allowlist.end(), uid);
}

// Send an event to the backend so it knows what the door saw locally
void publishEvent(const String &uid, bool authorized, const String &reason) {
  StaticJsonDocument<256> doc;
  doc["device_id"] = DEVICE_ID;
  doc["uid"] = uid;
  doc["authorized"] = authorized;
  doc["reason"] = reason;
  doc["ts"] = static_cast<uint32_t>(millis() / 1000);

  char out[256];
  size_t n = serializeJson(doc, out, sizeof(out));
  mqtt.publish(topicOf("events").c_str(), out, n);
}

// Request remote authorization for a uid (even if locally allowed)
void publishAccessRequest(const String &uid) {
  StaticJsonDocument<256> doc;
  doc["device_id"] = DEVICE_ID;
  doc["uid"] = uid;
  doc["ts"] = static_cast<uint32_t>(millis() / 1000);
  doc["allowlist_version"] = allowlistVersion;

  char out[256];
  size_t n = serializeJson(doc, out, sizeof(out));
  mqtt.publish(topicOf("access/request").c_str(), out, n);
}

// Prepare to receive an allowlist update split across MQTT chunks
void startChunkReceive(uint32_t version, int total) {
  chunkState.active = true;
  chunkState.version = version;
  chunkState.total = total;
  chunkState.uids.clear();
  chunkState.uids.reserve(200);
}

// React to a version announcement; chunks follow afterwards
void handleAllowlistVersion(const JsonVariant &v) {
  uint32_t ver = v["version"] | 0;
  Serial.printf("Allowlist version announce remote=%lu local=%lu\n", static_cast<unsigned long>(ver), static_cast<unsigned long>(allowlistVersion));
  if (ver <= allowlistVersion) {
    return;
  }

  int count = v["count"] | 0;
  (void)count;
}

// Collect partial allowlist data until all chunks arrive
void handleAllowlistChunk(const JsonVariant &v) {
  uint32_t ver = v["version"] | 0;
  int total = v["total"] | 0;
  int index = v["index"] | -1;

  if (!chunkState.active || chunkState.version != ver) {
    startChunkReceive(ver, total);
  }

  JsonArray arr = v["uids"].as<JsonArray>();
  for (JsonVariant x : arr) {
    String uid = String(static_cast<const char *>(x));
    uid.trim();
    if (!uid.isEmpty()) {
      chunkState.uids.push_back(uid);
    }
  }

  Serial.printf(
      "Allowlist chunk: ver=%lu index=%d total=%d collected=%u\n",
      static_cast<unsigned long>(ver), index, total,
      static_cast<unsigned>(chunkState.uids.size()));
}

// Deduplicate, persist, and activate the newly received allowlist
void applyAllowlist(uint32_t ver) {
  std::sort(chunkState.uids.begin(), chunkState.uids.end());
  chunkState.uids.erase(std::unique(chunkState.uids.begin(), chunkState.uids.end()), chunkState.uids.end());

  if (fileWriteAllowlist(chunkState.uids)) {
    allowlistVersion = ver;
    prefs.putUInt("al_ver", allowlistVersion);
    allowlist = chunkState.uids;
    Serial.printf("Allowlist applied ver=%lu count=%u\n", static_cast<unsigned long>(ver), static_cast<unsigned>(allowlist.size()));
  }

  chunkState.active = false;
}

// Central MQTT handler that routes administrative topics
void mqttCallback(char *topic, byte *payload, unsigned int length) {
  Serial.printf("MQTT message topic=%s len=%u\n", topic, length);
  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    Serial.println("JSON parse error from MQTT payload");
    return;
  }

  String t(topic);

  if (t.endsWith("/allowlist/version")) {
    handleAllowlistVersion(doc.as<JsonVariant>());
    return;
  }

  if (t.endsWith("/allowlist/chunk")) {
    handleAllowlistChunk(doc.as<JsonVariant>());
    return;
  }

  if (t.endsWith("/allowlist/apply")) {
    uint32_t ver = doc["version"] | 0;
    if (chunkState.active && chunkState.version == ver) {
      applyAllowlist(ver);
    }
    return;
  }

  if (t.endsWith("/access/response")) {
    return;
  }
}

// Subscribe once to every topic this device cares about
void subscribeToTopics() {
  const char *suffixes[] = {
      "allowlist/version",
      "allowlist/chunk",
      "allowlist/apply",
      "access/response"};

  for (const char *suffix : suffixes) {
    String topic = topicOf(suffix);
    bool ok = mqtt.subscribe(topic.c_str());
    Serial.printf("Subscribed %s (%s)\n", topic.c_str(), ok ? "ok" : "fail");
  }
}

// Keep trying to establish MQTT
void mqttConnect() {
  while (!mqtt.connected()) {
    String clientId = String("esp32_") + DEVICE_ID + "_" + String(static_cast<uint32_t>(ESP.getEfuseMac()), HEX);
    Serial.printf("Connecting to MQTT %s:%u as %s\n", MQTT_HOST, MQTT_PORT, clientId.c_str());

    if (mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
      Serial.printf("MQTT connected to %s:%u as %s\n", MQTT_HOST, MQTT_PORT, clientId.c_str());
      subscribeToTopics();
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

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS failed to mount");
  }

  prefs.begin("access", false);
  allowlistVersion = prefs.getUInt("al_ver", 0);

  fileLoadAllowlist();

  wifiConnect();

  SPI.begin();
  rfid.PCD_Init();
  Serial.println("RFID reader ready");

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
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
    bool localOk = isAuthorizedLocal(uid);

    if (localOk) {
      publishEvent(uid, true, "local_allowlist");
      // TODO: drive physical lock hardware here when authorized
    } else {
      publishEvent(uid, false, "local_denied");
    }

    publishAccessRequest(uid);
    delay(250);
  }
}