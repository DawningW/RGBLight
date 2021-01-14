/*
 * RGBLight with ESP8266 by wc
 * 
 * Made for my sisters
 */

#include <Ticker.h>
#include "LittleFS.h"
#include "ESP8266WiFi.h"
#include "ESP8266mDNS.h"
#include "ESP8266WebServer.h"
#include "WebSocketsServer.h"
#include "ArduinoJson.h"
#define FASTLED_ESP8266_NODEMCU_PIN_ORDER
// #define FASTLED_HAS_CLOCKLESS
#include "FastLED.h"

#include <vector>

#define _DEBUG
#ifdef _DEBUG
#include <GDBStub.h>
#endif

// LED_BUILTIN GPIO2/TXD // Cannot use Serial and the blue LED at the same time
#define POWER_LED_PIN 5 // D1 GPIO5
#define COLOR_LED_PIN 4 // D2 GPIO4
#define LED_TYPE WS2812
#define LED_COLOR_ORDER GRB
#define LED_COUNT 8

#define WIFI_AP_NAME "RGBLight"
#define WIFI_TIMEOUT 10000

const char NAME[] = "rgblight";

enum LightType {
    STATIC
};

// The settings of the RGBLight
struct Config {
    String ssid;
    String password;
    LightType mode;
    byte brightness;
    int fps;
    vector<int> animation;
} config;

Ticker timer;
short currentFrame = 0;
CRGB leds[LED_COUNT];

ESP8266WebServer webServer(80);
WebSocketsServer wsServer(81);

void readSettings() {
    Serial.println("Reading settings.");
    File file = LittleFS.open("/config.json", "r");
    StaticJsonDocument<400> doc;
    DeserializationError error = deserializeJson(doc, file);
    if (error) {
        Serial.println("Can't read settings.");
        Serial.println(error.f_str());
    }
    config.ssid = doc["ssid"] | "";
    config.password = doc["password"] | "";
    config.mode = doc["mode"] | STATIC;
    config.brightness = doc["brightness"] | 10;
    config.fps = doc["fps"] | 20;
    file.close();

    File file2 = LittleFS.open("/animation.json", "r");
    String str = file2.readString();
    file2.close();
}

void saveSettings() {
    Serial.println("Saving settings.");
    File file = LittleFS.open("/config.json", "w");
    StaticJsonDocument<400> doc;
    doc["ssid"] = config.ssid;
    doc["password"] = config.password;
    doc["mode"] = config.mode;
    doc["brightness"] = config.brightness;
    doc["fps"] = config.fps;
    if (!serializeJson(doc, file)) {
        Serial.println("Can't save settings.");
    }
    file.close();
}

void scanWifi() {
    Serial.println("Start to scan wifi.");
    int n = WiFi.scanNetworks();
    if (n > 0) {
        Serial.print(n);
        Serial.println(" wifi found.");
        for (int i = 0; i < n; ++i) {
            Serial.printf("%d: %s (%d)%c", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i),
                (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? ' ' : '*');
            Serial.println();
            delay(10);
        }
    } else {
        Serial.println("No wifi found.");
    }
    Serial.println("Scan finished.");
}

bool connectWifi(String& ssid, String& password) {
    Serial.println("Connecting to wlan.");
    if (ssid.isEmpty()) {
        Serial.println("No wifi stored.");
        return false;
    }
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        if (millis() - t > WIFI_TIMEOUT) {
            Serial.println();
            Serial.println("Can't connect to wlan.");
            return false;
        }
    }
    WiFi.hostname(WIFI_AP_NAME);
    Serial.println();
    Serial.println("Connect to wlan successfully.");
    return true;
}

bool startHotspot() {
    Serial.println("Start AP mode.");
    WiFi.mode(WIFI_AP);
    bool result = WiFi.softAP(WIFI_AP_NAME);
    delay(500);
    return result;
}

void webServerHandler() {
    if (LittleFS.exists(F("/index.html"))) {
        File file = LittleFS.open(F("/index.html"), "r");
        webServer.streamFile(file, "text/html");
        file.close();
    } else {
        webServer.send(404, "text/plain", "index.html not found.");
    }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED: {
            IPAddress ip = wsServer.remoteIP(num);
            Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\r\n", num, ip[0], ip[1], ip[2], ip[3], payload);

            wsServer.sendTXT(num, "Connect successfully!!!");
            break;
        }
        case WStype_DISCONNECTED: {
            Serial.printf("[%u] Disconnected!\r\n", num);
            break;
        }
        case WStype_TEXT: {
            Serial.printf("[%u] get Text: %s\r\n", num, payload);
            if (payload[0] == 'y') {
                digitalWrite(POWER_LED_PIN, HIGH);
            } else if (payload[0] == 'n') {
                digitalWrite(POWER_LED_PIN, LOW);
            }

            /*
            if (payload[0] == '#') {
                // we get RGB data

                // decode rgb data
                uint32_t rgb = (uint32_t) strtol((const char *) &payload[1], NULL, 16);
                Serial.printf("Received data: %d\r\n", rgb);
            }
            */
            break;
        }
    }
}

bool startServer(const char* domain) {
    Serial.println("Start web server.");
    webServer.on("/", webServerHandler);
    webServer.begin();
    Serial.println("Start web socket server.");
    wsServer.onEvent(webSocketEvent);
    wsServer.begin();
    Serial.println("Start mDNS server.");
    if (MDNS.begin(NAME)) {
        Serial.println("MDNS responder started.");
        MDNS.addService("http", "tcp", 80);
        MDNS.addService("ws", "tcp", 81);
    }
    return true;
}

void playLight() {
    // FastLED.show();
}

void initLight() {
    FastLED.setBrightness(config.brightness);
    currentFrame = 0;
    if (timer.active()) timer.detach();
    timer.attach_ms(1000 / config.fps, playLight);
}

void setup() {
    pinMode(POWER_LED_PIN, OUTPUT);
    pinMode(COLOR_LED_PIN, OUTPUT);
    digitalWrite(POWER_LED_PIN, HIGH);

    Serial.begin(9600);
    Serial.println();
#ifdef _DEBUG
    Serial.setDebugOutput(true);
    gdbstub_init();
#endif

    LittleFS.begin();
    bool isInited = LittleFS.exists("/config.json");
    readSettings();
    if (!isInited) saveSettings();
    
    scanWifi(); // TEST

    if (connectWifi(config.ssid, config.password)) {
        Serial.println(WiFi.SSID());
        Serial.println(WiFi.localIP());
    } else {
        startHotspot();
        Serial.println(WiFi.softAPSSID());
        Serial.println(WiFi.softAPIP());
    }
    startServer(NAME);

    LEDS.addLeds<LED_TYPE, COLOR_LED_PIN, LED_COLOR_ORDER>(leds, LED_COUNT);
    initLight();
}

void loop() {
    MDNS.update();
    webServer.handleClient();
    wsServer.loop();
}
