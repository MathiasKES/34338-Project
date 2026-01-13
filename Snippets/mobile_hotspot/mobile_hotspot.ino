#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

// ---------- CONFIG ----------
const char* AP_SSID = "ESP8266_AP";
const char* AP_PASS = "esp8266pass";

// ---------- SERVER ----------
ESP8266WebServer server(80);

// ---------- PROCESSING STATE ----------
int lastValue = 0;

// ---------- HANDLERS ----------

void handleRoot() {
  String html =
    "<!DOCTYPE html>"
    "<html>"
    "<head><title>ESP8266 AP</title></head>"
    "<body>"
    "<h1>ESP8266 Hotspot</h1>"
    "<p>Status: Online</p>"
    "<p>Last value: " + String(lastValue) + "</p>"
    "</body></html>";

  server.send(200, "text/html", html);
}

void handleData() {
  if (!server.hasArg("value")) {
    server.send(400, "text/plain", "Missing 'value' parameter");
    return;
  }

  int input = server.arg("value").toInt();

  // --- Simple processing ---
  lastValue = input * 2;

  String response =
    "Received: " + String(input) +
    "\nProcessed (x2): " + String(lastValue);

  server.send(200, "text/plain", response);
}

void handleJSON() {
  String json =
    "{"
    "\"status\":\"ok\","
    "\"last_value\":" + String(lastValue) +
    "}";

  server.send(200, "application/json", json);
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

// ---------- SETUP ----------
void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println();
  Serial.println("Starting ESP8266 Access Point...");

  // Start WiFi AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  IPAddress ip = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(ip);

  // Routes
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/json", handleJSON);
  server.onNotFound(handleNotFound);

  // Start server
  server.begin();
  Serial.println("HTTP server started");
}

// ---------- LOOP ----------
void loop() {
  server.handleClient();
}
