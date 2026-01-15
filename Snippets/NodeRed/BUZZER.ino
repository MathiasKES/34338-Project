#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

const char* ssid = "PET vogn 11";
const char* password = "vandmelon";

const char* mqttHost = "maqiatto.com";
const uint16_t mqttPort = 1883;

const char* mqttUser = "hectorfoss@gmail.com";
const char* mqttPass = "potter";

const char* subTopic = "hectorfoss@gmail.com/site1/door1/access/response";

const int BUZZER_PIN = 38;

WiFiClient espClient;
PubSubClient client(espClient);

void buzzOnce() {
  Serial.println("Buzzer: ON");
  digitalWrite(BUZZER_PIN, HIGH);
  delay(500);
  digitalWrite(BUZZER_PIN, LOW);
  Serial.println("Buzzer: OFF");
}


void callback(char* topic, byte* payload, unsigned int length) {
  if (length == 0) return;

  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, payload, length);

  if (err) {
    Serial.print("JSON parse failed: ");
    Serial.println(err.c_str());
    return;
  }

  bool authorized = false;

  if (doc["payload"].is<JsonObject>()) {
    authorized = doc["payload"]["authorized"] | false;
  } else {
    authorized = doc["authorized"] | false;
  }

  Serial.print("authorized: ");
  Serial.println(authorized ? "true" : "false");

  if (authorized) buzzOnce();
}


void printMqttState(int state) {
  Serial.print("MQTT state: ");
  Serial.print(state);
  Serial.print(" | ");

  switch (state) {
    case 0:  Serial.println("Connected"); break;
    case -1: Serial.println("Connection timeout"); break;
    case -2: Serial.println("Connection failed"); break;
    case -3: Serial.println("Disconnected"); break;
    case -4: Serial.println("Connection lost"); break;
    case -5: Serial.println("Connect failed"); break;
    default: Serial.println("Unknown"); break;
  }
}

void connectWifi() {
  Serial.println();
  Serial.print("WiFi: connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int dots = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
    dots++;
    if (dots % 40 == 0) Serial.println();
  }

  Serial.println();
  Serial.println("WiFi: connected");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("RSSI: ");
  Serial.println(WiFi.RSSI());
}

void reconnectMqtt() {
  while (!client.connected()) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi: lost connection, reconnecting");
      connectWifi();
    }

    uint64_t mac = ESP.getEfuseMac();
    char clientId[40];
    snprintf(clientId, sizeof(clientId), "esp32s3-buzzer-%04X%08X",
             (uint16_t)(mac >> 32), (uint32_t)mac);

    Serial.println();
    Serial.print("MQTT: connecting as ");
    Serial.println(clientId);
    Serial.print("MQTT host: ");
    Serial.print(mqttHost);
    Serial.print(" port: ");
    Serial.println(mqttPort);
    Serial.print("MQTT user: ");
    Serial.println(mqttUser);

    bool ok = client.connect(clientId, mqttUser, mqttPass);

    if (ok) {
      Serial.println("MQTT: connected");
      Serial.print("MQTT: subscribing to ");
      Serial.println(subTopic);

      if (client.subscribe(subTopic)) Serial.println("MQTT: subscribe OK");
      else Serial.println("MQTT: subscribe FAILED");
    } else {
      Serial.println("MQTT: connect FAILED");
      printMqttState(client.state());
      Serial.println("Retrying in 2s");
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("Boot: starting ESP32 S3");
  Serial.println("If you can read this, Serial is working");

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  buzzOnce();

  connectWifi();

  client.setServer(mqttHost, mqttPort);
  client.setCallback(callback);

  reconnectMqtt();
}

void loop() {
  if (!client.connected()) reconnectMqtt();
  client.loop();
}
