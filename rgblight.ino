/*
 * RGBLight with ESP8266 by wc
 * 
 * Made for my sisters
 */
// #define _DEBUG
#ifdef _DEBUG
#define DEBUG_ESP_WIFI
#define DEBUG_ESP_PORT Serial
#include <GDBStub.h>
// TODO 日志
#endif

// #include <vector>
#include <Ticker.h>
#include "LittleFS.h"
#include "ESP8266WiFi.h"
#include "ESP8266mDNS.h"
#include "ESP8266WebServer.h"
#include "WebSocketsServer.h"
#include "ArduinoJson.h"
#define FASTLED_ESP8266_RAW_PIN_ORDER // 不需要转换
#include "FastLED.h"

#define FOR_MY_SISTER
#define ID ""
#define MODEL "WC-5"
#ifdef FOR_MY_SISTER
#define NAME "姐姐妹妹的小彩灯~"
#else
#define NAME "RGBLight"
#endif
#define VERSION "V0.2"

// LED_BUILTIN GPIO2/TXD D4 // Cannot use Serial and the blue LED at the same time
#define POWER_LED_PIN 5 // GPIO5 D1
#define COLOR_LED_PIN 4 // GPIO4 D2
#define LED_TYPE WS2812B
#define LED_COLOR_ORDER GRB
#define LED_COUNT 30

#define CONFIG_SAVE_PERIOD 60 * 1000
#define HOST_NAME F("rgblight")
#define WIFI_TIMEOUT 10 * 1000

enum LightType {
    STATIC, // 常亮
    BLINK, // 闪烁
    BREATHE, // 呼吸
    CHASE, // 跑马灯
    RAINBOW, // 彩虹
    STREAM, // 流光
    ANIMATION, // 动画
    CUSTOM // 外部设备控制
};

// The settings of the RGBLight
struct Config {
    bool isDirty;
    time_t lastSaveTime;

    String name;
    String ssid;
    String password;
    LightType mode;
    uint8_t brightness;
    uint8_t refreshRate;
} config;

Ticker timer;
ESP8266WebServer webServer(80);
WebSocketsServer wsServer(81);

CRGB leds[LED_COUNT];
uint16_t currentFrame = 0;

String serialBuffer = "";

uint32_t rgb2hex(int r, int g, int b)
{   
    return ((r & 0xff) << 16) + ((g & 0xff) << 8) + (b & 0xff);
}

void readSettings() {
    Serial.println(F("Reading settings."));
    File file = LittleFS.open("/config.json", "r");
    StaticJsonDocument<400> doc;
    DeserializationError error = deserializeJson(doc, file);
    if (error) {
        Serial.println(F("Can't read settings."));
        Serial.println(error.f_str());
    } else {
        serializeJson(doc, Serial);
        Serial.println();
    }
    config.name = doc["name"] | NAME;
    config.ssid = doc["ssid"] | "";
    config.password = doc["password"] | "";
    config.mode = doc["mode"] | STATIC;
    config.brightness = doc["brightness"] | 100;
    config.refreshRate = doc["refreshRate"] | 60;
    file.close();
}

void saveSettings() {
    Serial.println(F("Saving settings."));
    File file = LittleFS.open("/config.json", "w");
    StaticJsonDocument<400> doc;
    doc["name"] = config.name;
    doc["ssid"] = config.ssid;
    doc["password"] = config.password;
    doc["mode"] = config.mode;
    doc["brightness"] = config.brightness;
    doc["refreshRate"] = config.refreshRate;
    if (!serializeJson(doc, file)) {
        Serial.println(F("Can't save settings."));
    }
    file.close();
    config.isDirty = false;
}

void markDirty() {
    config.lastSaveTime = millis();
    config.isDirty = true;
}

void loadAnimation(String &str) {
    // TODO 从文件读取动画
}

String scanWifi() {
    Serial.println(F("Start to scan wifi."));
    DynamicJsonDocument doc(1024);
    JsonArray array = doc.to<JsonArray>();
    int n = WiFi.scanNetworks();
    if (n > 0) {
        Serial.printf("%d wifi found.", n);
        Serial.println();
        for (int i = 0; i < n; ++i) {
            JsonObject obj = array.createNestedObject();
            obj["ssid"] = WiFi.SSID(i);
            obj["rssi"] = WiFi.RSSI(i);
            obj["type"] = WiFi.encryptionType(i);
            Serial.printf("%d: %s (%d)%c", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i),
                (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? ' ' : '*');
            Serial.println();
            delay(10);
        }
    } else {
        Serial.println(F("No wifi found."));
    }
    Serial.println(F("Scan wifi finished."));
    String jsonStr;
    serializeJson(doc, jsonStr);
    return jsonStr;
}

bool connectWifi(String &ssid, String &password) {
    Serial.println(F("Connecting to wlan."));
    if (ssid.isEmpty()) {
        Serial.println(F("Wifi SSID is empty."));
        return false;
    }
    WiFi.begin(ssid, password);
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        if (millis() - t > WIFI_TIMEOUT) {
            Serial.println();
            Serial.println(F("Can't connect to wlan."));
            return false;
        }
    }
    WiFi.hostname(HOST_NAME);
    Serial.println();
    Serial.println(F("Connect to wlan successfully."));
    return true;
}

bool openHotspot() {
    Serial.println(F("Start wifi hotspot."));
    bool result = WiFi.softAP(config.name);
    Serial.println(result);
    delay(500);
    return result;
}

void sendMessage(int receiver, const char *msg) {
    // TODO 先这样吧
    if (receiver == -1) {
        Serial.print("wc:");
        Serial.println(msg);
    }
    else {
        wsServer.sendTXT(receiver, msg);
    }
}

void commandHandler(int sender, String &msg) {
    char *command = (char*) malloc(msg.length() * sizeof(char));
    strcpy(command, msg.c_str());
    int argc = 0;
    char *argv[11];
    argv[argc] = strtok(command, " ");
    while (argv[argc] != NULL)
    {
        ++argc;
        argv[argc] = strtok(NULL, " ");
    }
    if (strcmp(argv[0], "info") == 0) {
        sendMessage(sender, "{}");
    }
    else if (strcmp(argv[0], "mode") == 0) {
        if (argc > 1) {
            // 设置灯的模式
            if (strcmp(argv[1], "static") == 0) {
                config.mode = STATIC;
            }
            else if (strcmp(argv[1], "stream") == 0) {
                config.mode = STREAM;
            }
            markDirty();
        }
        else {
            // 获取灯的模式
            String str(config.mode);
            sendMessage(sender, str.c_str());
        }
    }
    else if (strcmp(argv[0], "color") == 0) {
        if (argc > 1) {
            // 设置灯的颜色
            uint32_t rgb = (uint32_t) strtol(&argv[1][1], NULL, 16);
            CRGB color(rgb);
            for (int i = 0; i < LED_COUNT; ++i) {
                leds[i] = color;
            }
            markDirty();
        }
        else {
            // 获取灯的颜色
            uint32_t hex = rgb2hex(leds[0].r, leds[0].g, leds[0].b);
            char str[8];
            sprintf(str, "#%06x", hex);
            sendMessage(sender, str);
        }
    }
    else if (strcmp(argv[0], "scan") == 0) {
        String result = scanWifi();
        sendMessage(sender, result.c_str());
    }
    else if (strcmp(argv[0], "connect") == 0) {
        if (argc >= 3) {
            String ssid(argv[1]);
            String password(argv[2]);
            if (connectWifi(ssid, password)) {
                sendMessage(sender, WiFi.SSID().c_str());
                sendMessage(sender, WiFi.localIP().toString().c_str());
                WiFi.mode(WIFI_STA);
                config.ssid = ssid;
                config.password = password;
                markDirty();
            }
            else {
                sendMessage(sender, "连接失败!");
            }
        }
    }
    else if (strcmp(argv[0], "disconnect") == 0) {
        openHotspot();
        sendMessage(sender, WiFi.softAPSSID().c_str());
        sendMessage(sender, WiFi.softAPIP().toString().c_str());
        WiFi.mode(WIFI_AP);
        config.ssid = "";
        config.password = "";
        markDirty();
    }
    free(command);
}

void webServerHandler() {
    if (LittleFS.exists("/index.html")) {
        File file = LittleFS.open("/index.html", "r");
        webServer.streamFile(file, "text/html");
        file.close();
    } else {
        webServer.send(404, "text/plain", "index.html not found.");
    }
}

void webSocketHandler(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED: {
            IPAddress ip = wsServer.remoteIP(num);
            // payload is "/"
            Serial.printf("Client %u connected from %d.%d.%d.%d.", num, ip[0], ip[1], ip[2], ip[3]);
            Serial.println();
            wsServer.sendTXT(num, "Connect successfully!");
            break;
        }
        case WStype_DISCONNECTED: {
            Serial.printf("Client %u has disconnected.", num);
            Serial.println();
            break;
        }
        case WStype_TEXT: {
            String message(reinterpret_cast<char*>(payload));
            message.trim();
            if (!message.isEmpty()) {
                Serial.printf("Received message from %u: %s.", num, message.c_str());
                Serial.println();
                commandHandler(num, message);
            }
            break;
        }
    }
}

uint8_t currentHue = 0;

void updateLight() {
    if (++currentFrame > config.refreshRate) currentFrame = 0;
    // TODO 灯板刷新
    if (config.mode == STATIC) {

    }
    else if (config.mode == RAINBOW) {

    }
    else if (config.mode == STREAM) {
        // fill_gradient_HSV(leds, LED_COUNT, c1, c2);
        fill_rainbow(leds, LED_COUNT, currentHue++);
    }
    FastLED.show();
}

void setBrightness(uint8_t brightness) {
    FastLED.setBrightness(brightness);
    config.brightness = brightness;
    markDirty();
}

void setRefreshRate(uint8_t rate) {
    currentFrame = 0;
    if (timer.active()) timer.detach();
    timer.attach_ms(1000 / rate, updateLight);
    config.refreshRate = rate;
    markDirty();
}

void setMode(LightType type) {
    config.mode = type;
    markDirty();
}

void setup() {
    pinMode(POWER_LED_PIN, OUTPUT);
    digitalWrite(POWER_LED_PIN, HIGH);
    FastLED.addLeds<LED_TYPE, COLOR_LED_PIN, LED_COLOR_ORDER>(leds, LED_COUNT);
    // FastLED.setCorrection(); // TODO 颜色纠正
    FastLED.clear(true);
    delay(200);

    Serial.begin(9600);
    Serial.println();
    Serial.println();
#ifdef _DEBUG
    Serial.setDebugOutput(true);
    gdbstub_init();
#endif

    LittleFS.begin();
    readSettings();
    if (!LittleFS.exists("/config.json"))
        saveSettings();

    File file = LittleFS.open("/animation.csv", "r");
    String str = file.readString();
    loadAnimation(str);
    file.close();

    FastLED.setBrightness(config.brightness);
    timer.attach_ms(1000 / config.refreshRate, updateLight);
    fill_solid(leds, LED_COUNT, CRGB::White);

    if (connectWifi(config.ssid, config.password)) {
        WiFi.mode(WIFI_STA);
        Serial.println(WiFi.SSID());
        Serial.println(WiFi.localIP());
    } else {
        openHotspot();
        WiFi.mode(WIFI_AP);
        Serial.println(WiFi.softAPSSID());
        Serial.println(WiFi.softAPIP());
    }

    Serial.println(F("Start web server."));
    webServer.on("/", webServerHandler);
    webServer.begin();
    Serial.println(F("Start web socket server."));
    wsServer.onEvent(webSocketHandler);
    wsServer.begin();
    Serial.println(F("Start mDNS server."));
    if (MDNS.begin(HOST_NAME)) {
        MDNS.addService("http", "tcp", 80);
        MDNS.addService("ws", "tcp", 81);
        Serial.println(F("MDNS responder started."));
    }
}

void loop() {
    if (config.isDirty && millis() - config.lastSaveTime > CONFIG_SAVE_PERIOD) {
        saveSettings();
    }
    while (Serial.available() > 0) {
        char c = Serial.read();
        if (c != '\n') serialBuffer += c;
        else {
            // if (*(--serialBuffer.end()) == '\r') // 我觉得没有必要
            serialBuffer.trim();
            if (!serialBuffer.isEmpty()) {
                Serial.printf("Received data from com: %s.", serialBuffer.c_str());
                Serial.println();
                commandHandler(-1, serialBuffer);
                serialBuffer.clear();
            }
        }
    }
    MDNS.update();
    webServer.handleClient();
    wsServer.loop();
}
