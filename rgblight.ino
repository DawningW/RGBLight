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
#define VERSION "V0.5"
#ifdef FOR_MY_SISTER
#define NAME "姐姐妹妹的小彩灯~"
#else
#define NAME "RGBLight"
#endif
#define HOST_NAME "rgblight"

// LED_BUILTIN GPIO2/TXD D4 // Cannot use Serial and the blue LED at the same time
#define POWER_LED_PIN 5 // GPIO5 D1
#define COLOR_LED_PIN 4 // GPIO4 D2
#define LED_TYPE WS2812B
#define LED_COLOR_ORDER GRB
#define LED_COUNT 30 // TODO 支持LED方阵
// #define LED_CORRECTION 0xFFFFFF
#define DEFAULT_COLOR 0xFFFFFF

#define CONFIG_SAVE_PERIOD 30 * 1000
#define SERIAL_BUFFER 128
#define WIFI_TIMEOUT 10 * 1000
#define SSDP_XML "description.xml"

enum LightType {
    CONSTANT,    // 常亮
    BLINK,       // 闪烁
    BREATH,      // 呼吸
    CHASE,       // 跑马灯
    RAINBOW,     // 彩虹
    STREAM,      // 流光
    ANIMATION,   // 动画
    MUSIC,       // 音乐律动
    CUSTOM,      // 外部设备控制
    LIGHTS_COUNT
};
const char* LIGHT_TYPE_MAP[] = {"constant", "blink", "breath", "chase", "rainbow", "stream", "animation", "music", "custom"};

struct Config {
    bool isDirty;
    bool isLightDirty;
    time_t lastModifyTime;

    String name;
    String ssid;
    String password;
    String hostname;
    IPAddress ip;
    IPAddress gateway;
    IPAddress netmask;
    LightType mode;
    uint8_t brightness;
    uint32_t temperature;
    uint8_t refreshRate;
} config;

union {
    struct {
        CRGB currentColor;
    } constant;
    struct {
        CRGB currentColor;
        float lastTime;
        float interval;
    } blink;
    struct {
        CRGB currentColor;
        float lastTime;
        float interval;
    } breath;
    struct {
        CRGB currentColor;
        float lastTime;
    } chase;
    struct {
        uint8_t currentHue;
        int8_t delta;
    } rainbow;
    struct {
        uint8_t currentHue;
        int8_t delta;
    } stream;
    struct {
        uint32_t (*anim)[LED_COUNT];
        uint16_t frames;
    } animation;
    struct {
        bool type; // 0-电平模式 1-频谱模式
        uint8_t currentHue;
        double currentVolume; // Must be 0~1
    } music;
    struct {
        Sender *sender;
        uint8_t index;
    } custom;
} light;

Ticker timer;
CRGB leds[LED_COUNT];
uint16_t currentFrame = 0;
char serialBuffer[SERIAL_BUFFER + 1];
uint8_t serialBufPos = 0;
CommandHandler commandHandler;
ESP8266WebServer webServer(80);
WebSocketsServer wsServer(81);

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

/* Get light mode enum from string */
inline LightType str2mode(const char *str) {
    String str2(str);
    str2.toLowerCase();
    for (int i = 0; i < LIGHTS_COUNT; ++i) {
        if (strcmp(str2.c_str(), LIGHT_TYPE_MAP[i]) == 0) {
            return (LightType) i;
        }
    }
    return LIGHTS_COUNT;
}

/* Convert RGB(R, G, B) to hex(0xRRGGBB) */
inline uint32_t rgb2hex(uint8_t r, uint8_t g, uint8_t b) {   
    return ((r & 0xff) << 16) + ((g & 0xff) << 8) + (b & 0xff);
}

/* Convert RGB string(#RRGGBB) to hex(0xRRGGBB) */
inline uint32_t str2hex(const char *str) {
    if (str[0] != '#') return DEFAULT_COLOR;
    return (uint32_t) strtol(str + 1, NULL, 16);
}

/* Convert hex(0xRRGGBB) to RGB string(#RRGGBB) */
inline void hex2str(uint32_t hex, char *str) {
    sprintf(str, "#%06x", hex);
}

/*
 * Convert Kelvin(K) to hex(0xRRGGBB)
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
    StaticJsonDocument<600> doc;
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
    config.hostname = doc["hostname"] | HOST_NAME;
    String ip = doc["ip"];
    String gateway = doc["gateway"];
    String netmask = doc["netmask"];
    config.ip.fromString(ip);
    config.gateway.fromString(gateway);
    config.netmask.fromString(netmask);
    config.mode = doc["mode"] | CONSTANT;
    config.brightness = doc["brightness"] | 150;
    config.temperature = doc["temperature"] | 6600;
    config.refreshRate = doc["refreshRate"] | 60;
    file.close();
}

void saveSettings() {
    Serial.println(F("Saving settings."));
    File file = LittleFS.open("/config.json", "w");
    StaticJsonDocument<600> doc;
    doc["name"] = config.name;
    doc["ssid"] = config.ssid;
    doc["password"] = config.password;
    doc["hostname"] = config.hostname;
    doc["ip"] = config.ip ? config.ip.toString() : "";
    doc["gateway"] = config.gateway ? config.gateway.toString() : "";
    doc["netmask"] = config.netmask ? config.netmask.toString() : "";
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

void loadAnimation(String &str) {
    // 从字符串读取动画
}

void saveAnimation(String &str) {
    File file = LittleFS.open("/animation.csv", "w");
    file.print(str);
    file.close();
}

void readLights() {
    Serial.println(F("Reading light."));
    if (config.mode == ANIMATION) {
        File file = LittleFS.open("/animation.csv", "r");
        String str = file.readString();
        loadAnimation(str);
        file.close();
    } else if (config.mode != CUSTOM) {
        File file = LittleFS.open("/light.bin", "r");
        file.read((uint8_t*) &light, sizeof(light));
        file.close();
    }
}

void saveLights() {
    Serial.println(F("Saving light."));
    if (config.mode == ANIMATION) {
        // We don't save animation there
    } else if (config.mode != CUSTOM) {
        File file = LittleFS.open("/light.bin", "w");
        file.write((const uint8_t*) &light, sizeof(light));
        file.close();
    }
    config.isLightDirty = false;
}

void markDirty(bool isLight = false) {
    config.lastModifyTime = millis();
    if (isLight) config.isLightDirty = true;
    else config.isDirty = true;
}

void handleCommand(const Sender &sender, char *line) {
    if (config.mode == MUSIC) {
        char c = tolower(line[0]);
        if (c != 'm' && c != 'i') {
            light.music.currentVolume = atof(line);
            return;
        }
    } else if (config.mode == CUSTOM) {
        char c = tolower(line[0]);
        if (c != 'm' && c != 'i') {
            uint32_t color = str2hex(line);
            leds[light.custom.index++] = CRGB(color);
            if (light.custom.index >= LED_COUNT) light.custom.index = 0;
            return;
        }
    }
    commandHandler.parseCommand(sender, line);
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
#ifdef DEBUG_WC
                Serial.printf("Received data from com: %s.", serialBuffer);
#endif
                Serial.println();
                handleCommand(ss, serialBuffer);
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
            Serial.printf("Client %u connected from %s.", num, ip.toString().c_str());
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
            char *str = static_cast<char*>(payload);
            if (length > 0) {
#ifdef DEBUG_WC
                Serial.printf("Received message from %u: %s.", num, str);
                Serial.println();
#endif
                WebSocketSender ws(num);
                handleCommand(ws, str);
            }
            break;
        }
        default: break;
    }
}

void showSystemInfo() {
    Serial.println("----- System information -----");
    Serial.printf("Supply voltage: %.3fV\r\n", ESP.getVcc() / 1000.0);
    Serial.printf("Reset reason: %s\r\n", ESP.getResetReason().c_str());
    Serial.printf("Chip ID: %u\r\n", ESP.getChipId());
    Serial.printf("Core version: %s\r\n", ESP.getCoreVersion().c_str());
    Serial.printf("SDK version: %s\r\n", ESP.getSdkVersion());
    Serial.printf("CPU Freq: %dMHz\r\n", ESP.getCpuFreqMHz());
    Serial.printf("Free heap: %u\r\n", ESP.getFreeHeap());
    Serial.printf("Heap fragment percent: %d%%\r\n", ESP.getHeapFragmentation());
    Serial.printf("Max free block size: %d\r\n", ESP.getMaxFreeBlockSize());
    Serial.printf("Sketch size: %u\r\n", ESP.getSketchSize());
    Serial.printf("Sketch free space: %u\r\n", ESP.getFreeSketchSpace());
    Serial.printf("Sketch MD5: %s\r\n", ESP.getSketchMD5().c_str());
    Serial.printf("Flash chip ID: %u\r\n", ESP.getFlashChipId());
    Serial.printf("Flash chip size: %u\r\n", ESP.getFlashChipSize());
    Serial.printf("Flash chip real size: %u\r\n", ESP.getFlashChipRealSize());
    Serial.printf("Flash chip speed: %u\r\n", ESP.getFlashChipSpeed());
    Serial.printf("Cycle count: %u\r\n", ESP.getCycleCount());
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
    if (config.ip && config.gateway && config.netmask) {
        WiFi.config(config.ip, config.gateway, config.netmask);
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
    WiFi.hostname(config.hostname);
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

void showWifiInfo() {
    Serial.printf("Current mode: %d\r\n", WiFi.getMode());
    Serial.println("----- STA Info -----");
    Serial.printf("Connected: %s\r\n", WiFi.isConnected() ? "true" : "false");
    const char *wifi_status;
    switch (WiFi.status()) {
        case WL_IDLE_STATUS: wifi_status = "Idle";
        case WL_NO_SSID_AVAIL: wifi_status = "No SSID";
        case WL_SCAN_COMPLETED: wifi_status = "Scan Done";
        case WL_CONNECTED: wifi_status = "Connected";
        case WL_CONNECT_FAILED: wifi_status = "Failed";
        case WL_CONNECTION_LOST: wifi_status = "Lost";
        case WL_DISCONNECTED: wifi_status = "Disconnected";
        default: wifi_status = "Other";
    }
    Serial.printf("Connection status: %s\r\n", wifi_status);
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
    currentFrame = 0;
    if (config.mode != mode) {
        config.mode = mode;
        markDirty();
    }
}

void setConstant(uint32_t color = DEFAULT_COLOR) {
    light.constant.currentColor = CRGB(color);
    markDirty(true);
}

void setBlink(uint32_t color = DEFAULT_COLOR, float lastTime = 1.0, float interval = 1.0) {
    light.blink.currentColor = CRGB(color);
    light.blink.lastTime = lastTime > 0 ? lastTime : 1.0;
    light.blink.interval = interval >= 0 ? interval : 1.0;
    markDirty(true);
}

void setBreath(uint32_t color = DEFAULT_COLOR, float lastTime = 1.0, float interval = 0.5) {
    light.breath.currentColor = CRGB(color);
    light.breath.lastTime = lastTime > 0 ? lastTime : 1.0;
    light.breath.interval = interval >= 0 ? interval : 0.5;
    markDirty(true);
}

void setChase(uint32_t color = DEFAULT_COLOR, float lastTime = 0.2) {
    light.chase.currentColor = CRGB(color);
    light.chase.lastTime = lastTime ? lastTime : 0.2;
    markDirty(true);
}

void setRainbow(int8_t delta = 1) {
    light.rainbow.delta = delta != 0 ? delta : 1;
    markDirty(true);
}

void setStream(int8_t delta = 1) {
    light.stream.delta = delta != 0 ? delta : 1;
    markDirty(true);
}

void setMusic(uint8_t type = 1) {
    light.music.type = type;
    light.music.currentHue = 0;
    light.music.currentVolume = 0.0;
    markDirty(true);
}

void setBrightness(uint8_t brightness) {
    if (config.brightness != brightness) {
        FastLED.setBrightness(brightness);
        FastLED.show();
        config.brightness = brightness;
        markDirty();
    }
}

void setTemperature(uint32_t temperature) {
    if (config.temperature != temperature) {
        FastLED.setTemperature(CRGB(kelvin2rgb(temperature)));
        FastLED.show();
        config.temperature = temperature;
        markDirty();
    }
}

void setRefreshRate(uint8_t rate) {
    if (config.refreshRate != rate) {
        currentFrame = 0;
        if (timer.active()) timer.detach();
        timer.attach_ms(1000 / rate, updateLight);
        config.refreshRate = rate;
        markDirty();
    }
}

void updateLight() {
    if (config.mode == CONSTANT) {
        if (leds[0] != light.constant.currentColor) {
            fill_solid(leds, LED_COUNT, light.constant.currentColor);
            FastLED.show();
        }
    } else if (config.mode == BLINK) {
        int lastTime = config.refreshRate * light.blink.lastTime;
        int interval = config.refreshRate * light.blink.interval;
        if (currentFrame == 0) {
            fill_solid(leds, LED_COUNT, light.blink.currentColor);
            FastLED.show();
        } else if (currentFrame == lastTime) {
            fill_solid(leds, LED_COUNT, CRGB::Black);
            FastLED.show();
        }
        if (++currentFrame >= lastTime + interval) currentFrame = 0;
    } else if (config.mode == BREATH) {
        int lastTime = config.refreshRate * light.breath.lastTime;
        int interval = config.refreshRate * light.breath.interval;
        if (currentFrame <= lastTime) {
            CRGB rgb = light.breath.currentColor;
            double x = (double) currentFrame / lastTime;
            int scale = -1010 * x * x + 1010 * x;
            rgb.nscale8(scale);
            fill_solid(leds, LED_COUNT, rgb);
            FastLED.show();
        }
        if (++currentFrame >= lastTime + interval) currentFrame = 0;
    } else if (config.mode == CHASE) {
        int lastTime = config.refreshRate * light.chase.lastTime;
        if (currentFrame % lastTime == 0) {
            fill_solid(leds, LED_COUNT, CRGB::Black);
            int index = currentFrame / lastTime;
            if (index > LED_COUNT * 2 - 1) {
                currentFrame = 0;
                index = 0;
            } else if (index > LED_COUNT - 1) {
                index = 59 - index;
            }
            leds[index] = light.chase.currentColor;
            FastLED.show();
        }
        ++currentFrame;
    } else if (config.mode == RAINBOW) {
        CHSV hsv(light.rainbow.currentHue, 255, 240);
        CRGB rgb;
        hsv2rgb_rainbow(hsv, rgb);
        fill_solid(leds, LED_COUNT, rgb);
        FastLED.show();
        light.rainbow.currentHue += light.rainbow.delta;
    } else if (config.mode == STREAM) {
        fill_rainbow(leds, LED_COUNT, light.stream.currentHue);
        FastLED.show();
        light.stream.currentHue += light.stream.delta;
    } else if (config.mode == ANIMATION) {
        // 播放动画
    } else if (config.mode == MUSIC) {
        if (light.music.type) {
            // TODO 音乐律动可以优化一下
            // 分奇偶讨论???
            int count = LED_COUNT * light.music.currentVolume;
            CHSV hsv(light.music.currentHue++, 255, 240);
            CRGB rgb;
            hsv2rgb_rainbow(hsv, rgb);
            fill_solid(leds, LED_COUNT, CRGB::Black);
            fill_solid(leds + (LED_COUNT - count) / 2, count, rgb);
            FastLED.show();
        } else {
            int count = LED_COUNT * light.music.currentVolume;
            fill_solid(leds, LED_COUNT, CRGB::Black);
            if (count > 0) {
                fill_solid(leds, count - 1, CRGB::Green);
                leds[count - 1] = CRGB::Red;
            }
            FastLED.show();
        }
    } else if (config.mode == CUSTOM) {
        FastLED.show();
    }
}

ADC_MODE(ADC_VCC);
void preinit() {
    ESP8266WiFiClass::preinitWiFiOff();
}

// TODO 改造命令返回值
void registerCommands() {
    commandHandler.setDefaultHandler([](const Sender &sender, int argc, char *argv[]) {
        sender.send("Unknown command. type 'help' for helps.");
    });
    commandHandler.registerCommand("help", "Show command helps", [](const Sender &sender, int argc, char *argv[]) {
        commandHandler.printHelp(sender);
    });
    commandHandler.registerCommand("restart", "Restart system", [](const Sender &sender, int argc, char *argv[]) {
        ESP.restart();
    });
    commandHandler.registerCommand("sysinfo", "Show system infomation", [](const Sender &sender, int argc, char *argv[]) {
        sender.send("请在串口查看!");
        showSystemInfo();
    });
    commandHandler.registerCommand("name", "Get/set device name", [](const Sender &sender, int argc, char *argv[]) {
        if (argc >= 1) {
            config.name = argv[0];
            if (argc >= 2) {
                config.hostname = argv[1];
            }
            markDirty();
        } else {
            String str = String("name ") + config.name + ' ' + config.hostname;
            sender.send(str.c_str());
        }
    });
    commandHandler.registerCommand("scan", "Scan wifi", [](const Sender &sender, int argc, char *argv[]) {
        String result = scanWifi();
        sender.send(result.c_str());
    });
    commandHandler.registerCommand("connect", "Connect to wifi", [](const Sender &sender, int argc, char *argv[]) {
        // FIXME 无密码怎么设置静态IP以及SSID有空格咋办
        // FIXME wifi连接失败后不会连回去
        if (argc >= 1) {
            String ssid(argv[0]);
            String password(argc >= 2 ? argv[1] : "");
            if (argc >= 5) {
                config.ip.fromString(argv[2]);
                config.gateway.fromString(argv[3]);
                config.netmask.fromString(argv[4]);
            }
            if (connectWifi(ssid, password)) {
                sender.send(WiFi.SSID().c_str());
                sender.send(WiFi.localIP().toString().c_str());
                WiFi.mode(WIFI_STA);
                SSDP.begin();
                config.ssid = ssid;
                config.password = password;
                markDirty();
            } else {
                config.ip = IPAddress();
                config.gateway = IPAddress();
                config.netmask = IPAddress();
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
        config.ip = IPAddress();
        config.gateway = IPAddress();
        config.netmask = IPAddress();
        markDirty();
    });
    commandHandler.registerCommand("netinfo", "Show wifi infomation", [](const Sender &sender, int argc, char *argv[]) {
        sender.send("请在串口查看!");
        showWifiInfo();
    });
    commandHandler.registerCommand("mode", "Get/set light mode", [](const Sender &sender, int argc, char *argv[]) {
        if (argc >= 1) {
            LightType mode = str2mode(argv[0]);
            setMode(mode);
            switch (mode) {
                case CONSTANT: {
                    if (argc >= 2) {
                        uint32_t color = str2hex(argv[1]);
                        setConstant(color);
                        break;
                    }
                    setConstant();
                    break;
                }
                case BLINK: {
                    if (argc >= 2) {
                        uint32_t color = str2hex(argv[1]);
                        if (argc >= 3) {
                            float lastTime = atof(argv[2]);
                            if (argc >= 4) {
                                float interval = atof(argv[3]);
                                setBlink(color, lastTime, interval);
                                break;
                            }
                            setBlink(color, lastTime);
                            break;
                        }
                        setBlink(color);
                        break;
                    }
                    setBlink();
                    break;
                }
                case BREATH: {
                    if (argc >= 2) {
                        uint32_t color = str2hex(argv[1]);
                        if (argc >= 3) {
                            float lastTime = atof(argv[2]);
                            if (argc >= 4) {
                                float interval = atof(argv[3]);
                                setBreath(color, lastTime, interval);
                                break;
                            }
                            setBreath(color, lastTime);
                            break;
                        }
                        setBreath(color);
                        break;
                    }
                    setBreath();
                    break;
                }
                case CHASE: {
                    if (argc >= 2) {
                        uint32_t color = str2hex(argv[1]);
                        if (argc >= 3) {
                            float lastTime = atof(argv[2]);
                            setChase(color, lastTime);
                            break;
                        }
                        setChase(color);
                        break;
                    }
                    setChase();
                    break;
                }
                case RAINBOW: {
                    if (argc >= 2) {
                        int8_t delta = atoi(argv[1]);
                        setRainbow(delta);
                        break;
                    }
                    setRainbow();
                    break;
                }
                case STREAM: {
                    if (argc >= 2) {
                        int8_t delta = atoi(argv[1]);
                        setStream(delta);
                        break;
                    }
                    setStream();
                    break;
                }
                case ANIMATION: {
                    // 开始读取动画
                    break;
                }
                case MUSIC: {
                    if (argc >= 2) {
                        int8_t type = atoi(argv[1]);
                        setMusic(type);
                        break;
                    }
                    setMusic();
                    break;
                }
                case CUSTOM: {
                    light.custom.sender = nullptr;
                    light.custom.index = 0;
                    break;
                }
                default: {
                    sender.send("Invaild mode");
                    break;
                }
            }
        } else {
            String str = String("mode ") + LIGHT_TYPE_MAP[config.mode];
            if (config.mode <= CHASE) {
                str += ' ';
                CRGB &currentColor = light.constant.currentColor;
                uint32_t hex = rgb2hex(currentColor.r, currentColor.g, currentColor.b);
                char color[8];
                hex2str(hex, color);
                str += color;
            }
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
            String str = String("brightness ") + config.brightness;
            sender.send(str.c_str());
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
            String str = String("temperature ") + config.temperature + 'K';
            sender.send(str.c_str());
        }
    });
    commandHandler.registerCommand("fps", "Get/set refresh rate", [](const Sender &sender, int argc, char *argv[]) {
        if (argc >= 1) {
            int rate = atoi(argv[0]);
            if (rate > 0 && rate <= 255) {
                setRefreshRate((uint8_t) rate);
            } else {
                sender.send("Invaild refresh rate");
            }
        } else {
            String str = String("fps ") + config.refreshRate;
            sender.send(str.c_str());
        }
    });
    commandHandler.registerCommand("info", "Show rgblight information", [](const Sender &sender, int argc, char *argv[]) {
        StaticJsonDocument<1024> doc;
        doc["name"] = config.name;
        doc["id"] = ID;
        doc["model"] = MODEL;
        doc["version"] = VERSION;
        doc["mode"] = LIGHT_TYPE_MAP[config.mode];
        if (config.mode <= CHASE) {
            CRGB &currentColor = light.constant.currentColor;
            uint32_t hex = rgb2hex(currentColor.r, currentColor.g, currentColor.b);
            char color[8];
            hex2str(hex, color);
            doc["color"] = color;
        }
        doc["brightness"] = config.brightness;
        doc["temperature"] = config.temperature;
        doc["fps"] = config.refreshRate;
        String jsonStr;
        serializeJson(doc, jsonStr);
        sender.send(jsonStr.c_str());
    });
}

void setup() {
    registerCommands();

    pinMode(POWER_LED_PIN, OUTPUT);
    digitalWrite(POWER_LED_PIN, HIGH);
    FastLED.addLeds<LED_TYPE, COLOR_LED_PIN, LED_COLOR_ORDER>(leds, LED_COUNT);
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
    readLights();

    FastLED.setBrightness(config.brightness);
    FastLED.setTemperature(CRGB(kelvin2rgb(config.temperature)));
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

    Serial.println(F("Start HTTP server."));
    webServer.on("/" SSDP_XML, HTTP_GET, []() {
        SSDP.schema(webServer.client());
    });
    webServer.serveStatic("/", LittleFS, "/www/", "max-age=300");
    webServer.begin();
    Serial.println(F("Start WebSocket server."));
    wsServer.onEvent(webSocketHandler);
    wsServer.begin();
    Serial.println(F("Start mDNS."));
    if (MDNS.begin(config.hostname)) {
        MDNS.addService("http", "tcp", 80);
        MDNS.addService("ws", "tcp", 81);
        Serial.println(F("MDNS responder started."));
    }
    Serial.println(F("Start SSDP."));
    SSDP.setDeviceType("upnp:rootdevice");
    SSDP.setSchemaURL(SSDP_XML);
    SSDP.setName(config.name);
    SSDP.setSerialNumber(ID);
    SSDP.setModelName(MODEL);
    SSDP.setModelNumber(VERSION);
    SSDP.setModelURL("https://github.com/DawningW/Microcontroller-Projects/tree/master/rgblight");
    SSDP.setManufacturer("Wu Chen");
    SSDP.setManufacturerURL("https://github.com/DawningW");
    SSDP.setURL("index.html");
    if (SSDP.begin()) {
        Serial.println(F("SSDP started."));
    }
}

void loop() {
    if (config.isDirty && millis() - config.lastModifyTime > CONFIG_SAVE_PERIOD) {
        saveSettings();
    }
    if (config.isLightDirty && millis() - config.lastModifyTime > CONFIG_SAVE_PERIOD) {
        saveLights();
    }
    readSerial();
    MDNS.update();
    webServer.handleClient();
    wsServer.loop();
}
