// === Include Required Libraries ===
#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Adafruit_NeoPixel.h>
#include <ESP8266Ping.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>

// === Constants ===
#define NUM_LEDS 10
#define LED_PIN D2
#define AP_SSID "WiFi_Statuslight"


#define E_OK      0     // Success
#define E_ERROR  -1     // Error
#define E_TIMEOUT -2    // Timeout
#define E_OTHER  -3     // Other/unexpected
#define E_BLACK -4      // this LED stays black (for null:// URLs)

// === Global Objects ===
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
AsyncWebServer server(80);

// === Global Variables ===
String ssid, password;
bool wifiConnected = false;

// === Configuration Structure ===
struct Config {
  String server[NUM_LEDS - 1];
  int timeout = 20;
  int brightness = 20;
  String hass_server_url;
  String hass_llat;
} config;


// === LED Utility Functions ===
void blue(int i) {
  strip.setPixelColor(i, strip.Color(0, 0, 255));
  strip.show();
}

void setAllLEDsColor(uint32_t color) {
  for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, color);
  strip.show();
}

// === WiFi Setup Functions ===
void startAPMode() {
  WiFi.softAP(AP_SSID);
  Serial.println("Started AP mode.");
}

// === WiFi Config Page Setup ===
void showWiFiConfigPage() {
  server.on("/", HTTP_GET, handleWiFiConfigPage);
  server.on("/save", HTTP_POST, handleWiFiCredentialsSave);
  server.begin();
}

// === Handler: Serve WiFi Config Page ===

void handleWiFiConfigPage(AsyncWebServerRequest *request) {
  String html = R"rawliteral(
  <!DOCTYPE html>
  <html>
  <head>
    <meta charset="UTF-8">
    <title>WiFi Statuslight - WiFi Configuration</title>
    <style>
      body { font-family: Arial, sans-serif; margin: 40px; }
      label { display: block; margin-top: 10px; }
      input[type="text"], input[type="password"] {
        width: 300px; padding: 8px; margin-top: 5px;
      }
      input[type="submit"] {
        margin-top: 20px; padding: 10px 20px;
      }
    </style>
  </head>
  <body>
    <h2>Enter WiFi Credentials</h2>
    <form action="/save" method="POST">
      <label for="ssid">WiFi SSID:</label>
      <input type="text" id="ssid" name="ssid" placeholder="Enter SSID" maxlength=""100" required>

      <label for="password">WiFi Password:</label>
      <input type="text" id="password" name="password" placeholder="Enter Password" maxlength=""100" required>

      <input type="submit" value="Save">
    </form>
  </body>
  </html>
  )rawliteral";
  request->send(200, "text/html", html);
}

// === Handler: Save WiFi Credentials ===
void handleWiFiCredentialsSave(AsyncWebServerRequest *request) {
  if (request->hasParam("ssid", true) && request->hasParam("password", true)) {
    ssid = request->getParam("ssid", true)->value();
    password = request->getParam("password", true)->value();
    saveWiFiCredentials();
    ESP.restart();
  } else {
    request->send(400, "text/plain", "Missing parameters");
  }
}

bool loadWiFiCredentials() {
  if (!LittleFS.exists("/wifi.json")) return false;
  File file = LittleFS.open("/wifi.json", "r");
  DynamicJsonDocument doc(512);
  deserializeJson(doc, file);
  ssid = doc["ssid"].as<String>();
  password = doc["password"].as<String>();
  file.close();
  return true;
}

void saveWiFiCredentials() {
  DynamicJsonDocument doc(512);
  doc["ssid"] = ssid;
  doc["password"] = password;
  File file = LittleFS.open("/wifi.json", "w");
  serializeJson(doc, file);
  file.close();
}

// === Configuration Page Setup ===
void showConfigPage() {
  server.on("/", HTTP_GET, handleConfigPage);
  server.on("/save_config", HTTP_POST, handleConfigSave);
  server.begin();
}

// === Handler: Serve Configuration Page ===
//void handleConfigPage(AsyncWebServerRequest *request) {
//  request->send(LittleFS, "/config.html", "text/html");
//}

void handleConfigPage(AsyncWebServerRequest *request) {
  String html = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
      <title>WiFi Statuslight Configuration</title>
      <style>
        body {
          font-family: Arial, sans-serif;
          background-color: #f4f4f4;
          margin: 0;
          padding: 20px;
        }
        h2 {
          color: #333;
        }
        form {
          background: #fff;
          padding: 20px;
          border-radius: 8px;
          max-width: 500px;
          margin: auto;
          box-shadow: 0 0 10px rgba(0,0,0,0.1);
        }
        label {
          display: block;
          margin-top: 15px;
          font-weight: bold;
        }
        input[type="text"],
        input[type="number"] {
          width: 100%;
          padding: 8px;
          margin-top: 5px;
          border: 1px solid #ccc;
          border-radius: 4px;
        }
        input[type="submit"] {
          margin-top: 20px;
          background-color: #4CAF50;
          color: white;
          padding: 10px 15px;
          border: none;
          border-radius: 4px;
          cursor: pointer;
        }
        input[type="submit"]:hover {
          background-color: #45a049;
        }
      </style>
    </head>
    <body>
      <h2>Device Configuration</h2>
      <form method='POST' action='/save_config'>
  )rawliteral";

  for (int i = 0; i < NUM_LEDS - 1; i++) {
    html += "<label for='server" + String(i) + "'>Server " + String(i) + ":</label>";
    html += "<input type='text' name='server" + String(i) + "' value='" + config.server[i] + "'>";
  }

  html += "<label for='timeout'>Timeout (1-60):</label>";
  html += "<input type='number' name='timeout' min='1' max='60' value='" + String(config.timeout) + "'>";

  html += "<label for='brightness'>Brightness (1-255):</label>";
  html += "<input type='number' name='brightness' min='1' max='255' value='" + String(config.brightness) + "'>";

  html += "<label for='hass_url'>HASS URL:</label>";
  html += "<input type='text' name='hass_url' value='" + config.hass_server_url + "'>";

  html += "<label for='hass_llat'>HASS LLAT:</label>";
  html += "<input type='text' name='hass_llat' value='" + config.hass_llat + "'>";

  html += "<input type='submit' value='Save'>";
  html += "</form>";
  html += "<p>URL syntax</p>";
  html += "http://servername.example.org/ or https://servername.example.org/index.html<br/>";
  html += "tcp://servername.example.org:port<br/>";
  html += "udp://servername.example.org:port<br/>";
  html += "ping://servername.example.org<br/>";
  html += "null://does_not_matter (LED remains bdark<br/>";
  html += "hass://api/states/entity_id/switch.switch_14 - this requires Home Assistant URL and access token to be set!<br/></p>";
  html += "</body></html>";

  request->send(200, "text/html", html);
}




// === Handler: Save Configuration Data ===
void handleConfigSave(AsyncWebServerRequest *request) {
  for (int i = 0; i < NUM_LEDS - 1; i++) {
    String paramName = "server" + String(i);
    if (request->hasParam(paramName, true)) {
      config.server[i] = request->getParam(paramName, true)->value();
    }
  }

  if (request->hasParam("timeout", true)) {
    config.timeout = constrain(request->getParam("timeout", true)->value().toInt(), 1, 60);
  }

  if (request->hasParam("brightness", true)) {
    config.brightness = constrain(request->getParam("brightness", true)->value().toInt(), 1, 255);
  }

  if (request->hasParam("hass_url", true)) {
    config.hass_server_url = request->getParam("hass_url", true)->value();
  }

  if (request->hasParam("hass_llat", true)) {
    config.hass_llat = request->getParam("hass_llat", true)->value();
  }

  saveConfig();
  delay(1000);
  ESP.restart();
}

void loadConfig() {
  Serial.println("Reading config...");
  if (!LittleFS.exists("/config.json")) {
    Serial.println("LittleFS file /config.json does no exist!");
    return;
  }
  File file = LittleFS.open("/config.json", "r");
  DynamicJsonDocument doc(1024 + NUM_LEDS * 128);
  deserializeJson(doc, file);
  for (int i = 0; i < NUM_LEDS - 1; i++) {
    config.server[i] = doc["server"][i].as<String>();
    Serial.printf("Server[%d[]=%s\n", i, config.server[i].c_str());
  }
  config.timeout = doc["timeout"]; Serial.printf("Timeout:%d\n",config.timeout);
  config.brightness = doc["brightness"]; Serial.printf("Brightness: %d\n",config.brightness);
  config.hass_server_url = doc["hass_url"].as<String>(); Serial.printf("HASS Server: %s\n",config.hass_server_url.c_str());
  config.hass_llat = doc["hass_llat"].as<String>(); Serial.printf("HASS LLAT: %s...\n", config.hass_llat.substring(0,10).c_str()); // show only the first few characters for (minimal) security
  file.close();
  Serial.println("Config read.");
}

void saveConfig() {
  Serial.println("Saving config...");
  DynamicJsonDocument doc(1024+128*NUM_LEDS);
  JsonArray arr = doc.createNestedArray("server");
  for (int i = 0; i < NUM_LEDS - 1; i++) arr.add(config.server[i]);
  doc["timeout"] = config.timeout;
  doc["brightness"] = config.brightness;
  doc["hass_url"] = config.hass_server_url;
  doc["hass_llat"] = config.hass_llat;
  File file = LittleFS.open("/config.json", "w");
  serializeJsonPretty(doc, Serial); // pretty-print the JSON document to the serial port
  serializeJson(doc, file);
  file.close();
}

// === Server Check Function ===
int checkServer(String url) {
  if (url.startsWith("http://")) {
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(config.timeout * 1000);
    http.begin(client, url);
    int code = http.GET();
    http.end();
    Serial.printf(" %d ", code);
    return code < 400 ? E_OK : E_ERROR;
  } else if (url.startsWith("https://")) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient https;
    https.setTimeout(config.timeout * 1000);
    https.begin(client, url);
    int code = https.GET();
    https.end();
    Serial.printf(" %d ", code);
    return code < 400 ? E_OK : E_ERROR;
  } else if (url.startsWith("tcp://")) {
    String host = url.substring(6);
    int colon = host.indexOf(':');
    String hostname = host.substring(0, colon);
    int port = host.substring(colon + 1).toInt();
    WiFiClient client;
    return client.connect(hostname.c_str(), port) ? E_OK : E_ERROR;
  } else if (url.startsWith("udp://")) {
    String host = url.substring(6);
    int colon = host.indexOf(':');
    String hostname = host.substring(0, colon);
    int port = host.substring(colon + 1).toInt();
    WiFiUDP udp;
    udp.beginPacket(hostname.c_str(), port);
    udp.write("ping");
    return udp.endPacket() ? E_OK : E_ERROR;
  } else if (url.startsWith("ping://")) {
    String host = url.substring(7);
    return Ping.ping(host.c_str()) ? E_OK : E_ERROR;
  } else if (url.startsWith("hass://")) {
    String path = url.substring(7);
    if (!path.startsWith("/")) path = "/" + path;
    String fullUrl = config.hass_server_url + path;
    // remove red and green states from URL 
    // Find the position of the second-to-last colon
    int lastColon = fullUrl.lastIndexOf(':');
    int secondLastColon = fullUrl.lastIndexOf(':', lastColon - 1);
    // If both colons are found, remove from secondLastColon to the end
    if (secondLastColon != -1) {
      fullUrl = fullUrl.substring(0, secondLastColon);
    }

    String greenState = path.substring(path.lastIndexOf(':') + 1);
    String redState = path.substring(path.lastIndexOf(':', path.lastIndexOf(':') - 1) + 1, path.lastIndexOf(':'));
    Serial.printf("HASS api call=%s red=%s green=%s\n", fullUrl.c_str(), redState.c_str(), greenState.c_str());

    //WiFiClientSecure client;
    //client.setInsecure();
    WiFiClient client;
    HTTPClient http;
    http.begin(client, fullUrl);
    http.addHeader("Authorization", "Bearer " + config.hass_llat);
    http.addHeader("Content-Type", "application/json");
    int code = http.GET();
    Serial.printf(" HTTP %d ", code);
    if (code != 200) return E_ERROR;
    String payload = http.getString();
    Serial.print(payload);
    http.end();

    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payload);
    String state = doc["state"];
    Serial.printf(" State=%s ", state.c_str());
    if (state == greenState) return E_OK;
    if (state == redState) return E_ERROR;
    return E_OTHER;
  } else if ((url == "") || url.startsWith("null://")) {
    return E_BLACK;
  }
  return E_TIMEOUT;
}

// === Setup ===
void setup() {
  blue(1);
  if(config.brightness <20) config.brightness=20;
  Serial.begin(115200);
  Serial.println("Starting Setup()");
  strip.begin();
  strip.setBrightness(config.brightness);
  strip.show();

  blue(2);
  if (!LittleFS.begin()) {
    Serial.println("LittleFS failed to mount.");
    return;
  }

  blue(3);
  if (!loadWiFiCredentials()) {
    Serial.println("No WiFi credaentials. Connect your WiFi client to built-in access point and provide some.");
    startAPMode();
    blue(5);
    showWiFiConfigPage();
    blue(6);
    return;
  }

  Serial.printf("Connecting to WiFi SSID=%s\n", ssid.c_str());
  WiFi.begin(ssid.c_str(), password.c_str());
  int attempts = 0;
  blue(7);
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  wifiConnected = WiFi.status() == WL_CONNECTED;
  if (wifiConnected) {
    Serial.println("WiFi connected.");
    setAllLEDsColor(strip.Color(0, 0, 0)); // Off
    loadConfig();
    showConfigPage();
  } else {
    Serial.println("WiFi failed.");
    setAllLEDsColor(strip.Color(255, 0, 0)); // Red
  }
}

// === Main Loop ===
void loop() {
  Serial.println("Loop()");
  // strip.clear();
  // dispaly WiFi status on LED #0
  strip.setBrightness(config.brightness);
  strip.setPixelColor(0, wifiConnected ? strip.Color(0, 255, 0) : strip.Color(255, 0, 0));
  strip.show();
  if (!wifiConnected) return;

  for (int i = 0; i < NUM_LEDS - 1; i++) {
    Serial.printf("Testing server %d: %s...", i, config.server[i].c_str());
    int result = checkServer(config.server[i]);
    uint32_t color;
    switch (result) {
      case E_OK:      color = strip.Color(0, 255, 0);     Serial.println("OK");break; // Green
      case E_ERROR:   color = strip.Color(255, 0, 0);     Serial.println("ERROR");break; // Red
      case E_TIMEOUT: color = strip.Color(255, 165, 0);   Serial.println("TIMEOUT");break; // Orange
      case E_BLACK:   color = strip.Color(0, 0, 0);       Serial.println("BLACK");break; // Black (dark)
      default:        color = strip.Color(255, 255, 255); Serial.println("WHITE");break; // White
    }
    strip.setPixelColor(i + 1, color);
    strip.show();
  }
  
  delay(5000);
}
