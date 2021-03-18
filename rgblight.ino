/*
 * RGBLight with ESP8266 by wc
 */
// #define DEBUG_WC
#ifdef DEBUG_WC
#define _DEBUG 1
#define DEBUG_ESP_WIFI
#define DEBUG_ESP_PORT Serial
#include <GDBStub.h>
#endif

#include <Arduino.h>
#include <Ticker.h>
#include <LittleFS.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266SSDP.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#define FASTLED_ESP8266_RAW_PIN_ORDER // 不需要转换
#include <FastLED.h>
#include "CommandHandler.h"

// #define FOR_MY_SISTER
#define ID "001234567890"
#define MODEL "WC-5"
#define VERSION "V0.4"
#ifdef FOR_MY_SISTER
#define NAME "姐姐妹妹的小彩灯~"
#else
#define NAME "RGBLight"
#endif

ADC_MODE(ADC_VCC);
// LED_BUILTIN GPIO2/TXD D4 // Cannot use Serial and the blue LED at the same time
#define POWER_LED_PIN 5 // GPIO5 D1
#define COLOR_LED_PIN 4 // GPIO4 D2
#define LED_TYPE WS2812B
#define LED_COLOR_ORDER GRB
#define LED_COUNT 30
// #define LED_CORRECTION 0xFFFFFF

#define CONFIG_SAVE_PERIOD 30 * 1000
#define SERIAL_BUFFER 128
#define WIFI_TIMEOUT 10 * 1000
#define HOST_NAME F("rgblight")

enum LightType {
    CONSTANT,  // 常亮
    BLINK,     // 闪烁
    BREATHE,   // 呼吸
    CHASE,     // 跑马灯
    RAINBOW,   // 彩虹
    STREAM,    // 流光
    ANIMATION, // 动画
    CUSTOM     // 外部设备控制
};

char serialBuffer[SERIAL_BUFFER + 1];
uint8_t serialBufPos = 0;
CommandHandler commandHandler;
ESP8266WebServer webServer(80);
WebSocketsServer wsServer(81);

struct Config {
    bool isDirty;
    time_t lastModifyTime;

    String name;
    String ssid;
    String password;
    LightType mode;
    uint8_t brightness;
    uint32_t temperature;
    uint8_t refreshRate;
} config;

struct {
    bool isDirty;
    time_t lastModifyTime;

    Ticker timer;
    CRGB leds[LED_COUNT];
    uint16_t currentFrame = 0;
    union {
        struct {
            CRGB currentColor;
        } constant;
        struct {
            CRGB currentColor;
            float interval;
        } blink;
        struct {
            CRGB currentColor;
            float interval;
        } breathe;
        struct {
            CRGB currentColor;
            bool reversed;
        } chase;
        struct {
            uint8_t currentHue;
        } rainbow;
        struct {
            uint8_t currentHue;
        } stream;
        struct {
            uint32_t (*animation)[LED_COUNT];
            uint16_t frames;
        } animation;
        struct {
            Sender *sender;
        } custom;
    };
} light;

class SerialSender: public Sender {
public:
    void send(const char *msg) const override {
        Serial.println(msg);
    }
} ss;

class WebSocketSender: public Sender {
private:
    int num;
public:
    WebSocketSender(int n): num(n) {}
    void send(const char *msg) const override {
        wsServer.sendTXT(num, msg, strlen(msg));
    }
};

/* Convert RGB(R, G, B) to hex(0xRRGGBB) */
inline uint32_t rgb2hex(uint8_t r, uint8_t g, uint8_t b) {   
    return ((r & 0xff) << 16) + ((g & 0xff) << 8) + (b & 0xff);
}

/* Convert RGB string(#RRGGBB) to hex(0xRRGGBB) */
inline uint32_t str2hex(const char *str) {
    return (uint32_t) strtol(str + 1, NULL, 16);
}

/* Convert hex(0xRRGGBB) to RGB string(#RRGGBB) */
inline void hex2str(uint32_t hex, char *str) {
    sprintf(str, "#%06x", hex);
}

/*
 * Convert Kelvin to hex(0xRRGGBB)
 * From http://www.tannerhelland.com/4435/convert-temperature-rgb-algorithm-code/
 */
uint32_t kelvin2rgb(uint32_t t) {
    double temperature = (double) t / 100;
    double red, green, blue;
    // Red
    if (temperature <= 66) {
        red = 255;
    } else {
        red = temperature - 60;
        red = 329.698727446 * pow(red, -0.1332047592);
        red = constrain(red, 0, 255);
    }
    // Green
    if (temperature <= 66) {
        green = temperature;
        green = 99.4708025861 * log(green) - 161.1195681661;
        green = constrain(green, 0, 255);
    } else {
        green = temperature - 60;
        green = 288.1221695283 * pow(green, -0.0755148492);
        green = constrain(green, 0, 255);
    }
    // Blue
    if (temperature >= 66) {
        blue = 255;
    } else if (temperature <= 19) {
        blue = 0;
    } else {
        blue = temperature - 10;
        blue = 138.5177312231 * log(blue) - 305.0447927307;
        blue = constrain(blue, 0, 255);
    }
    return rgb2hex((uint8_t) red, (uint8_t) green, (uint8_t) blue);
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
    config.mode = doc["mode"] | CONSTANT;
    config.brightness = doc["brightness"] | 150;
    config.temperature = doc["temperature"] | 6600;
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
    doc["temperature"] = config.temperature;
    doc["refreshRate"] = config.refreshRate;
    if (!serializeJson(doc, file)) {
        Serial.println(F("Can't save settings."));
    }
    file.close();
    config.isDirty = false;
}

void markDirty() {
    config.lastModifyTime = millis();
    config.isDirty = true;
}

void loadAnimation(String &str) {
    // TODO 从文件读取动画
}

void clearBuffer(bool clearAll = false) {
    serialBufPos = 0;
    if (clearAll) {
        memset(serialBuffer, 0, sizeof(serialBuffer));
        return;
    }
    serialBuffer[0] = '\0';
}

void readSerial() {
    while (Serial.available() > 0) {
        char c = Serial.read();
        if (c == '\r' || c == '\n') {
            serialBuffer[serialBufPos] = '\0';
            if (serialBufPos > 0) {
                Serial.printf("Received data from com: %s.", serialBuffer);
                Serial.println();
                commandHandler.parseCommand(ss, serialBuffer);
            }
            clearBuffer();
        } else if (serialBufPos < SERIAL_BUFFER && isprint(c)) {
            serialBuffer[serialBufPos++] = c;
        }
    }
}

void webSocketHandler(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED: {
            // payload is "/"
            IPAddress ip = wsServer.remoteIP(num);
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
            if (!message.isEmpty()) {
                Serial.printf("Received message from %u: %s.", num, message.c_str());
                Serial.println();
                WebSocketSender ws(num);
                commandHandler.parseCommand(ws, message);
            }
            break;
        }
    }
}

void sysinfo() {
    Serial.println("----- System information -----");
    Serial.printf("Supply voltage: %d", ESP.getVcc());
    Serial.printf("Reset reason: %s\r\n", ESP.getResetReason().c_str());
    Serial.printf("Chip ID: %d\r\n", ESP.getChipId());
    Serial.printf("Core version: %s\r\n", ESP.getCoreVersion().c_str());
    Serial.printf("SDK version: %s\r\n", ESP.getSdkVersion());
    Serial.printf("CPU Freq: %dMHz\r\n", ESP.getCpuFreqMHz());
    Serial.printf("Free heap: %d\r\n", ESP.getFreeHeap());
    Serial.printf("Heap fragment percent: %d\r\n", ESP.getHeapFragmentation());
    Serial.printf("Max free block size: %d\r\n", ESP.getMaxFreeBlockSize());
    Serial.printf("Sketch size: %d\r\n", ESP.getSketchSize());
    Serial.printf("Sketch free space: %d\r\n", ESP.getFreeSketchSpace());
    Serial.printf("Sketch MD5: %s\r\n", ESP.getSketchMD5().c_str());
    Serial.printf("Flash chip ID: %d\r\n", ESP.getFlashChipId());
    Serial.printf("Flash chip size: %d\r\n", ESP.getFlashChipSize());
    Serial.printf("Flash chip real size: %d\r\n", ESP.getFlashChipRealSize());
    Serial.printf("Flash chip speed: %d\r\n", ESP.getFlashChipSpeed());
    Serial.printf("Cycle count: %d\r\n", ESP.getCycleCount());
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

// TODO 静态IP地址
bool connectWifi(String &ssid, String &password) {
    Serial.println(F("Connecting to wlan."));
    if (ssid.isEmpty()) {
        Serial.println(F("Wifi SSID is empty."));
        return false;
    }
    WiFi.begin(ssid, password);
    time_t t = millis();
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

static const char* getWifiStatus(wl_status_t status) {
    switch (status) {
        case WL_IDLE_STATUS:
            return "Idle";
        case WL_NO_SSID_AVAIL:
            return "No SSID";
        case WL_SCAN_COMPLETED:
            return "Scan Done";
        case WL_CONNECTED:
            return "Connected";
        case WL_CONNECT_FAILED:
            return "Failed";
        case WL_CONNECTION_LOST:
            return "Lost";
        case WL_DISCONNECTED:
            return "Disconnected";
        default:
            return "Other";
    }
}

void showWifiInfo() {
    Serial.printf("Current mode: %d\r\n", WiFi.getMode());
    Serial.println("----- STA Info -----");
    Serial.printf("Connected: %s\r\n", WiFi.isConnected() ? "true" : "false");
    Serial.printf("Connection status: %s\r\n", getWifiStatus(WiFi.status()));
    Serial.printf("Auto connect: %s\r\n", WiFi.getAutoConnect() ? "true" : "false");
    Serial.printf("MAC address: %s\r\n", WiFi.macAddress().c_str());
    Serial.printf("Hostname: %s\r\n", WiFi.hostname().c_str());
    Serial.printf("SSID: %s\r\n", WiFi.SSID().c_str());
    Serial.printf("PSK: %s\r\n", WiFi.psk().c_str());
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("Local IP: %s\r\n", WiFi.localIP().toString().c_str());
        Serial.printf("Subnet mask: %s\r\n", WiFi.subnetMask().toString().c_str());
        Serial.printf("Gateway IP: %s\r\n", WiFi.gatewayIP().toString().c_str());
        Serial.printf("DNS 1: %s\r\n", WiFi.dnsIP(0).toString().c_str());
        Serial.printf("DNS 2: %s\r\n", WiFi.dnsIP(1).toString().c_str());
        Serial.printf("BSSID: %s\r\n", WiFi.BSSIDstr().c_str());
        Serial.printf("RSSI: %d\r\n", WiFi.RSSI());
    }
    Serial.println("----- AP Info -----");
    Serial.printf("Station count: %d\r\n", WiFi.softAPgetStationNum());
    Serial.printf("AP IP: %s\r\n", WiFi.softAPIP().toString().c_str());
    Serial.printf("AP MAC: %s\r\n", WiFi.softAPmacAddress().c_str());
    softap_config config;
    if (wifi_softap_get_config(&config)) {
        Serial.printf("SSID: %s\r\n", config.ssid);
        Serial.printf("Password: %s\r\n", config.password);
        Serial.printf("Channel: %d\r\n", config.channel);
        Serial.printf("Auth mode: %d\r\n", config.authmode);
        Serial.printf("Hidden: %d\r\n", config.ssid_hidden);
        Serial.printf("Max connections: %d\r\n", config.max_connection);
        Serial.printf("Beacon interval: %d\r\n", config.beacon_interval);
    }
}

void setMode(LightType mode) {
    // FIXME error: variable or field 'setMode' declared void
    // 'LightType' was not declared in this scope
    config.mode = mode;
    markDirty();
}

void setBrightness(uint8_t brightness) {
    FastLED.setBrightness(brightness);
    config.brightness = brightness;
    markDirty();
}

void setTemperature(uint32_t temperature) {
    FastLED.setTemperature(CRGB(kelvin2rgb(temperature)));
    config.temperature = temperature;
    markDirty();
}

void setRefreshRate(uint8_t rate) {
    light.currentFrame = 0;
    if (light.timer.active()) light.timer.detach();
    light.timer.attach_ms(1000 / rate, updateLight);
    config.refreshRate = rate;
    markDirty();
}

// @Deprecated 垃圾代码, 将会移除于改用mode命令设置灯的颜色后
CRGB* getColorPointer() {
    switch (config.mode) {
        case CONSTANT: return &light.constant.currentColor;
        case BLINK: return &light.blink.currentColor;
        case BREATHE: return &light.breathe.currentColor;
        case CHASE: return &light.chase.currentColor;
    }
    return nullptr;
}
// @Deprecated 我还在想灯模式怎么存储
void markLightDirty() {
    light.lastModifyTime = millis();
    light.isDirty = true;
}

void initLight() {
    File file = LittleFS.open("/animation.csv", "r");
    String str = file.readString();
    if (config.mode == ANIMATION) {
        loadAnimation(str);
    } else {
        CRGB *currentColor = getColorPointer();
        if (currentColor) {
            *currentColor = CRGB(str2hex(str.c_str()));
        }
    }
    file.close();
}

void updateLight() {
    if (++light.currentFrame >= config.refreshRate) light.currentFrame = 0;
    if (config.mode == CONSTANT) {
        if (light.leds[0] != light.constant.currentColor) {
            fill_solid(light.leds, LED_COUNT, light.constant.currentColor);
        }
    } else if (config.mode == BLINK) {
        if (light.currentFrame == 0) {
            fill_solid(light.leds, LED_COUNT, light.blink.currentColor);
        } else if (light.currentFrame == config.refreshRate / 2) {
            fill_solid(light.leds, LED_COUNT, CRGB::Black);
        }
    } else if (config.mode == BREATHE) {
        CRGB rgb = light.breathe.currentColor;
        int temp = 255 - 510 * light.currentFrame / config.refreshRate;
        rgb.nscale8(abs(temp));
        fill_solid(light.leds, LED_COUNT, rgb);
    } else if (config.mode == CHASE) {
        fill_solid(light.leds, LED_COUNT, CRGB::Black);
        int led = LED_COUNT * light.currentFrame / config.refreshRate;
        if (light.chase.reversed) led = LED_COUNT - 1 - led;
        light.leds[led] = light.chase.currentColor;
        if (light.currentFrame == config.refreshRate - 1)
            light.chase.reversed = !light.chase.reversed;
    } else if (config.mode == RAINBOW) {
        CHSV hsv(light.rainbow.currentHue++, 255, 240);
        CRGB rgb;
        hsv2rgb_rainbow(hsv, rgb);
        fill_solid(light.leds, LED_COUNT, rgb);
    } else if (config.mode == STREAM) {
        fill_rainbow(light.leds, LED_COUNT, light.stream.currentHue++);
    } else if (config.mode == ANIMATION) {
        // TODO 播放动画
    }
    FastLED.show();
}

void preinit() {
    ESP8266WiFiClass::preinitWiFiOff();

    commandHandler.registerCommand("sysinfo", "Show system infomation", [](const Sender &sender, int argc, char *argv[]) {
        sender.send("请在串口查看!");
        sysinfo();
    });
    commandHandler.registerCommand("scan", "Scan wifi", [](const Sender &sender, int argc, char *argv[]) {
        String result = scanWifi();
        sender.send(result.c_str());
    });
    commandHandler.registerCommand("connect", "Connect to wifi", [](const Sender &sender, int argc, char *argv[]) {
        if (argc >= 1) {
            String ssid(argv[0]);
            String password(argc >= 2 ? argv[1] : "");
            if (connectWifi(ssid, password)) {
                sender.send(WiFi.SSID().c_str());
                sender.send(WiFi.localIP().toString().c_str());
                WiFi.mode(WIFI_STA);
                config.ssid = ssid;
                config.password = password;
                markDirty();
            } else {
                sender.send("连接失败!");
            }
        }
    });
    commandHandler.registerCommand("disconnect", "Disconnect from wifi", [](const Sender &sender, int argc, char *argv[]) {
        openHotspot();
        sender.send(WiFi.softAPSSID().c_str());
        sender.send(WiFi.softAPIP().toString().c_str());
        WiFi.mode(WIFI_AP);
        config.ssid = "";
        config.password = "";
        markDirty();
    });
    commandHandler.registerCommand("netinfo", "Show wifi infomation", [](const Sender &sender, int argc, char *argv[]) {
        sender.send("请在串口查看!");
        showWifiInfo();
    });
    commandHandler.registerCommand("mode", "Get/set light mode", [](const Sender &sender, int argc, char *argv[]) {
        if (argc >= 1) {
            if (strcmp(argv[0], "constant") == 0) {
                setMode(CONSTANT);
            } else if (strcmp(argv[0], "blink") == 0) {
                setMode(BLINK);
            } else if (strcmp(argv[0], "breathe") == 0) {
                setMode(BREATHE);
            } else if (strcmp(argv[0], "chase") == 0) {
                setMode(CHASE);
            } else if (strcmp(argv[0], "rainbow") == 0) {
                setMode(RAINBOW);
            } else if (strcmp(argv[0], "stream") == 0) {
                setMode(STREAM);
            } else if (strcmp(argv[0], "animation") == 0) {
                setMode(ANIMATION);
            } else if (strcmp(argv[0], "custom") == 0) {
                setMode(CUSTOM);
            }
        } else {
            String str(config.mode);
            sender.send(str.c_str());
        }
    });
    commandHandler.registerCommand("brightness", "Get/set brightness", [](const Sender &sender, int argc, char *argv[]) {
        if (argc >= 1) {
            int brightness = atoi(argv[0]);
            if (brightness >= 0 && brightness <= 255) {
                setBrightness((uint8_t) brightness);
            } else {
                sender.send("Invaild brightness");
            }
        } else {
            char str[4];
            itoa(config.brightness, str, 10);
            sender.send(str);
        }
    });
    commandHandler.registerCommand("temperature", "Get/set temperature", [](const Sender &sender, int argc, char *argv[]) {
        if (argc >= 1) {
            int temperature = atoi(argv[0]);
            if (temperature >= 0) {
                setTemperature((uint32_t) temperature);
            } else {
                sender.send("Invaild temperature");
            }
        } else {
            char str[8];
            str[0] = 'K';
            itoa(config.temperature, str + 1, 10);
            sender.send(str);
        }
    });
    // @Deprecated 将被移除, 未来将改为使用mode设置颜色等参数
    commandHandler.registerCommand("color", "Get/set color(Will be removed in the future)", [](const Sender &sender, int argc, char *argv[]) {
        CRGB *currentColor = getColorPointer();
        if (!currentColor) {
            sender.send("该模式下无法获取/修改颜色");
            return;
        }
        if (argc >= 1) {
            uint32_t hex = str2hex(argv[0]);
            *currentColor = CRGB(hex);
            markLightDirty();
        } else {
            uint32_t hex = rgb2hex(currentColor->r, currentColor->g, currentColor->b);
            char str[8];
            hex2str(hex, str);
            sender.send(str);
        }
    });
}

void setup() {
    pinMode(POWER_LED_PIN, OUTPUT);
    digitalWrite(POWER_LED_PIN, HIGH);
    FastLED.addLeds<LED_TYPE, COLOR_LED_PIN, LED_COLOR_ORDER>(light.leds, LED_COUNT);
#ifdef LED_CORRECTION
    FastLED.setCorrection(CRGB(LED_CORRECTION));
#endif
    FastLED.clear(true);
    delay(200);

    clearBuffer(true);
    Serial.begin(9600);
    Serial.println();
    Serial.println();
#ifdef DEBUG_WC
    Serial.setDebugOutput(true);
    gdbstub_init();
#endif

    LittleFS.begin();
    readSettings();
    if (!LittleFS.exists("/config.json"))
        saveSettings();

    initLight();
    FastLED.setBrightness(config.brightness);
    FastLED.setTemperature(CRGB(kelvin2rgb(config.temperature)));
    light.timer.attach_ms(1000 / config.refreshRate, updateLight);

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

    Serial.println(F("Start HTTP server."));
    webServer.serveStatic("/", LittleFS, "/www/", "max-age=300");
    webServer.begin();
    Serial.println(F("Start WebSocket server."));
    wsServer.onEvent(webSocketHandler);
    wsServer.begin();
    Serial.println(F("Start mDNS."));
    if (MDNS.begin(HOST_NAME)) {
        MDNS.addService("http", "tcp", 80);
        MDNS.addService("ws", "tcp", 81);
        Serial.println(F("MDNS responder started."));
    }
    Serial.println(F("Start SSDP."));
    SSDP.setName(config.name);
    SSDP.setSerialNumber(ID);
    SSDP.setModelName(MODEL);
    SSDP.setModelNumber(VERSION);
    SSDP.setModelURL("https://github.com/DawningW/Microcontroller-Projects/tree/master/rgblight");
    SSDP.setManufacturer("Wu Chen");
    SSDP.setManufacturerURL("https://github.com/DawningW");
    SSDP.setURL("index.html");
    // SSDP.setHTTPPort(80);
    if (SSDP.begin()) {
        SSDP.schema(Serial);
        Serial.println(F("SSDP started."));
    }
}

void loop() {
    if (config.isDirty && millis() - config.lastModifyTime > CONFIG_SAVE_PERIOD) {
        saveSettings();
    }
    if (light.isDirty && millis() - light.lastModifyTime > 10 * 1000) {
        CRGB *currentColor = getColorPointer();
        if (currentColor) {
            File file = LittleFS.open("/animation.csv", "w");
            uint32_t hex = rgb2hex(currentColor->r, currentColor->g, currentColor->b);
            char str[8];
            hex2str(hex, str);
            file.print(str);
            file.close();
        }
        light.isDirty = false;
    }
    readSerial();
    MDNS.update();
    webServer.handleClient();
    wsServer.loop();
}
