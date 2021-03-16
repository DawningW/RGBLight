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
#define VERSION "V0.3"

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
CRGB currentColor = CRGB::White;
uint8_t currentHue = 0;
bool reversed = false;

String serialBuffer = "";

uint32_t rgb2hex(int r, int g, int b) {   
    return ((r & 0xff) << 16) + ((g & 0xff) << 8) + (b & 0xff);
}

uint32_t str2hex(const char *str) {
    return (uint32_t) strtol(str, NULL, 16);
}

void hex2str(uint32_t hex, char *str) {
    sprintf(str, "#%06x", hex);
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
            else if (strcmp(argv[1], "blink") == 0) {
                config.mode = BLINK;
            }
            else if (strcmp(argv[1], "breathe") == 0) {
                config.mode = BREATHE;
            }
            else if (strcmp(argv[1], "chase") == 0) {
                config.mode = CHASE;
            }
            else if (strcmp(argv[1], "rainbow") == 0) {
                config.mode = RAINBOW;
            }
            else if (strcmp(argv[1], "stream") == 0) {
                config.mode = STREAM;
            }
            else if (strcmp(argv[1], "animation") == 0) {
                config.mode = ANIMATION;
            }
            else if (strcmp(argv[1], "custom") == 0) {
                config.mode = CUSTOM;
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
            uint32_t hex = str2hex(&argv[1][1]);
            currentColor = CRGB(hex);
            markDirty();
        }
        else {
            // 获取灯的颜色
            uint32_t hex = rgb2hex(currentColor.r, currentColor.g, currentColor.b);
            char str[8];
            hex2str(hex, str);
            sendMessage(sender, str);
        }
    }
    else if (strcmp(argv[0], "brightness") == 0) {
        if (argc > 1) {
            // 设置灯的亮度
            int brightness = atoi(argv[1]);
            if (brightness >= 0 && brightness <= 255) {
                setBrightness((uint8_t) brightness);
            } else {
                sendMessage(sender, "Invaild brightness");
            }
        }
        else {
            // 获取灯的亮度
            char str[4];
            itoa(config.brightness, str, 10);
            sendMessage(sender, str);
        }
    }
    else if (strcmp(argv[0], "scan") == 0) {
        String result = scanWifi();
        sendMessage(sender, result.c_str());
    }
    else if (strcmp(argv[0], "connect") == 0) {
        if (argc >= 2) {
            String ssid(argv[1]);
            String password(argc >= 3 ? argv[2] : "");
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

void webServerHandler2() {
    if (LittleFS.exists("/bundle.js")) {
        File file = LittleFS.open("/bundle.js", "r");
        webServer.streamFile(file, "text/javascript");
        file.close();
    } else {
        webServer.send(404, "text/plain", "bundle.js not found.");
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

void updateLight() {
    if (++currentFrame >= config.refreshRate) currentFrame = 0;
    if (config.mode == STATIC) {
        if (leds[0] != currentColor) {
            fill_solid(leds, LED_COUNT, currentColor);
        }
    }
    else if (config.mode == BLINK) {
        if (currentFrame == 0) {
            fill_solid(leds, LED_COUNT, currentColor);
        }
        else if (currentFrame == config.refreshRate / 2) {
            fill_solid(leds, LED_COUNT, CRGB::Black);
        }
    }
    else if (config.mode == BREATHE) {
        CRGB rgb = currentColor;
        int temp = 255 - 510 * currentFrame / config.refreshRate;
        rgb.nscale8(abs(temp));
        fill_solid(leds, LED_COUNT, rgb);
    }
    else if (config.mode == CHASE) {
        fill_solid(leds, LED_COUNT, CRGB::Black);
        int led = LED_COUNT * currentFrame / config.refreshRate;
        if (!reversed) {
            leds[led] = currentColor;
        }
        else {
            leds[LED_COUNT - 1 - led] = currentColor;
        }
        if (currentFrame == config.refreshRate - 1) reversed = !reversed;
    }
    else if (config.mode == RAINBOW) {
        CHSV hsv(currentHue++, 255, 240);
        CRGB rgb;
        hsv2rgb_rainbow(hsv, rgb);
        fill_solid(leds, LED_COUNT, rgb);
        if (currentHue > 255) currentHue -= 255;
    }
    else if (config.mode == STREAM) {
        fill_rainbow(leds, LED_COUNT, currentHue++);
        if (currentHue > 255) currentHue -= 255;
    }
    else if (config.mode == ANIMATION) {

    }
    else if (config.mode == CUSTOM) {

    }
    FastLED.show();
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
    webServer.on("/bundle.js", webServerHandler2);
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
