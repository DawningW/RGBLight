/**
 * RGB Light 炫酷 RGB 灯
 *
 * 基于 ESP8266 使用 Arduino 开发的物联网炫酷小彩灯, 支持多种形态多种光效, 配套自研网页/小程序/PC 客户端
 *
 * @author QingChenW
 */

#include "config.h"

#include <functional>
#include <Arduino.h>
#include <Ticker.h>
#include <LittleFS.h>
#include <Updater.h>
#include <DNSServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#define FASTLED_ESP8266_RAW_PIN_ORDER // 不需要转换
#include <FastLED.h>
#ifdef ENABLE_DEBUG
#include <GDBStub.h>
#endif

#include "CommandHandler.hpp"
#include "Light.hpp"
#include "LightEffect.hpp"
#include "utils.h"

#define MIME_TYPE(t) (mime::mimeTable[mime::type::t].mimeType)

typedef std::function<Effect*(int argc, const char *argv[])> CreateEffectFunc;

const char *product_name = "rgblight";
const char *model_name = MODEL;
const char *version = VERSION;
const uint32_t version_code = VERSION_CODE;

CreateEffectFunc effectFactories[EFFECT_TYPE_COUNT];
Ticker timer;
LIGHT_TYPE light;
Effect *lightEffect;
DNSServer dnsServer;
ESP8266WebServer webServer(80);
WebSocketsServer wsServer(81);

struct Config {
    time_t lastModifyTime;
    bool isDirty;

    String name;          // 设备名称
    String ssid;          // WIFI 名称
    String password;      // WIFI 密码
    String hostname;      // 主机名
    uint16_t refreshRate; // 刷新率, 默认 60Hz
    uint8_t brightness;   // 亮度, 默认 63
    uint32_t temperature; // 色温, 默认 6600K
} config;

// For LightEffect
const uint16_t &fps = config.refreshRate;

void markDirty() {
    config.lastModifyTime = millis();
    config.isDirty = true;
}

void serializeSettings(JsonDocument &doc, bool includeWifi = true) {
    doc["name"] = config.name;
    if (includeWifi) { // 获取 wifi 信息时不应包含密码
        doc["ssid"] = config.ssid;
        doc["password"] = config.password;
    }
    doc["hostname"] = config.hostname;
    doc["refreshRate"] = config.refreshRate;
    doc["brightness"] = config.brightness;
    doc["temperature"] = config.temperature;
    lightEffect->writeToJSON(doc);
}

void saveSettings() {
    Serial.println(F("Save settings"));
    StaticJsonDocument<1024> doc;
    doc["version"] = version_code;
    serializeSettings(doc);
    File file = LittleFS.open("/config.json", "w");
    if (!serializeJson(doc, file)) {
        Serial.println(F("Save settings failed"));
    }
    file.close();
    config.isDirty = false;
}

void readSettings() {
    Serial.println(F("Read settings"));
    bool shouldSave = false;
    StaticJsonDocument<1024> doc;
    if (!LittleFS.exists("/config.json")) {
        Serial.println(F("Setting not found, create new"));
        shouldSave = true;
    } else {
        File file = LittleFS.open("/config.json", "r");
        DeserializationError error = deserializeJson(doc, file);
        file.close();
        if (error) {
            Serial.print(F("Read settings failed: "));
            Serial.println(error.f_str());
            shouldSave = true;
        }
    }
    if (doc["version"] != version_code) {
        Serial.printf_P(PSTR("Update settings from %d to %d\n"), doc["version"].as<int>(), version_code);
        shouldSave = true;
    }
    config.name = doc["name"] | NAME;
    config.ssid = doc["ssid"].as<const char *>();
    config.password = doc["password"].as<const char *>();
    config.hostname = doc["hostname"] | product_name;
    config.refreshRate = doc["refreshRate"] | 60;
    config.brightness = doc["brightness"] | 63;
    config.temperature = doc["temperature"] | 6600;
    lightEffect = Effect::readFromJSON<LIGHT_TYPE>(doc);

    FastLED.setBrightness(config.brightness);
    FastLED.setTemperature(CRGB(kelvin2rgb(config.temperature)));
    timer.attach_ms(1000 / config.refreshRate, updateLight);
    
    if (shouldSave) {
        saveSettings();
    }
}

// 2.7.4 版本 MDNS 服务需要绑定到接口, 所以在每次切换接口时重启 MDNS 服务
void setWifiMode(WiFiMode_t mode) {
    dnsServer.stop();
    MDNS.end();

    WiFi.mode(mode);
    WiFi.hostname(config.hostname);
    delay(100);

    if (mode == WIFI_AP) {
        Serial.println(F("Start DNS server"));
        dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
        if (dnsServer.start(53, "*", WiFi.softAPIP())) {
            Serial.println(F("DNS server started"));
        }
    }
    Serial.println(F("Start mDNS"));
    if (MDNS.begin(config.hostname)) {
        MDNS.addService("http", "tcp", 80);
        // MDNS.addService("ws", "tcp", 81);
        MDNS.addServiceTxt("http", "tcp", "product", product_name);
        MDNS.addServiceTxt("http", "tcp", "version", version);
        MDNS.addServiceTxt("http", "tcp", "name", config.name);
        Serial.println(F("MDNS responder started"));
    }
}

int scanWifi(JsonArray &array) {
    Serial.println(F("Start to scan wifi"));
    int n = WiFi.scanNetworks();
    if (n > 0) {
        Serial.printf_P(PSTR("%d wifi(s) found:\n"), n);
        for (int i = 0; i < n; ++i) {
            JsonObject obj = array.createNestedObject();
            obj["ssid"] = WiFi.SSID(i);
            obj["rssi"] = WiFi.RSSI(i);
            obj["type"] = WiFi.encryptionType(i);
            Serial.printf_P(PSTR("%d: %s (%d)%c\n"), i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i),
                (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? ' ' : '*');
        }
    } else {
        Serial.println(F("No wifi found"));
    }
    Serial.println(F("Scan wifi finished"));
    return n;
}

bool connectWifi(const String &ssid, const String &password) {
    Serial.println(F("Connecting to wlan"));
    if (ssid.isEmpty()) {
        Serial.println(F("Wifi SSID is empty"));
        return false;
    }
    WiFi.begin(ssid, password);
    time_t t = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        if (millis() - t > 10 * 1000) {
            Serial.println();
            Serial.println(F("Can't connect to wlan"));
            return false;
        }
    }
    Serial.println();
    Serial.print(F("Connect to wlan successfully, IP: "));
    Serial.println(WiFi.localIP());
    return true;
}

bool startHotspot() {
    Serial.println(F("Start wifi hotspot"));
    bool result = WiFi.softAP(config.name);
    if (result) {
        Serial.print(F("Start hotspot successfully, IP: "));
        Serial.println(WiFi.softAPIP());
    } else {
        Serial.println(F("Can't start hotspot"));
    }
    return result;
}

void updateLight() {
    if (lightEffect->update(light, 0)) {
        FastLED.show();
    }
}

void handleCommand(SenderFunc sender, char *line) {
    if (lightEffect->type() == MUSIC) {
        if (!isalpha(line[0])) { // 假定所有命令都是字母开头且以字母开头的一定是命令
            ((MusicEffect<LIGHT_TYPE> *) lightEffect)->setVolume(atof(line));
            return;
        }
    } else if (lightEffect->type() == CUSTOM) {
        if (!isalpha(line[0])) {
            uint32_t color = str2hex(line);
            int &index = ((CustomEffect<LIGHT_TYPE> *) lightEffect)->getIndex();
            light.data()[index++] = CRGB(color);
            if (index >= light.count()) {
                index = 0;
            }
            return;
        }
    }
    cmdHandler.parseCommand(sender, line);
}

void initEffects() {
    effectFactories[CONSTANT] = [](int argc, const char *argv[]) {
        uint32_t color = argc > 0 ? str2hex(argv[0]) : DEFAULT_COLOR;
        return new ConstantEffect<LIGHT_TYPE>(color);
    };
    effectFactories[BLINK] = [](int argc, const char *argv[]) {
        uint32_t color = argc > 0 ? str2hex(argv[0]) : DEFAULT_COLOR;
        float lastTime = argc > 1 ? atof(argv[1]) : 1.0;
        float interval = argc > 2 ? atof(argv[2]) : 1.0;
        return new BlinkEffect<LIGHT_TYPE>(color, lastTime, interval);
    };
    effectFactories[BREATH] = [](int argc, const char *argv[]) {
        uint32_t color = argc > 0 ? str2hex(argv[0]) : DEFAULT_COLOR;
        float lastTime = argc > 1 ? atof(argv[1]) : 1.0;
        float interval = argc > 2 ? atof(argv[2]) : 0.5;
        return new BreathEffect<LIGHT_TYPE>(color, lastTime, interval);
    };
    effectFactories[CHASE] = [](int argc, const char *argv[]) {
        uint32_t color    = argc > 0 ? str2hex(argv[0]) : DEFAULT_COLOR;
        uint8_t direction = argc > 1 ? atoi(argv[1]) : 0;
        float lastTime    = argc > 2 ? atof(argv[2]) : 0.2;
        return new ChaseEffect<LIGHT_TYPE>(color, direction, lastTime);
    };
    effectFactories[RAINBOW] = [](int argc, const char *argv[]) {
        int8_t delta = argc > 0 ? atoi(argv[0]) : 1;
        return new RainbowEffect<LIGHT_TYPE>(delta);
    };
    effectFactories[STREAM] = [](int argc, const char *argv[]) {
        uint8_t direction = argc > 0 ? atoi(argv[0]) : 0;
        int8_t delta      = argc > 1 ? atoi(argv[1]) : 1;
        return new StreamEffect<LIGHT_TYPE>(direction, delta);
    };
    effectFactories[ANIMATION] = [](int argc, const char *argv[]) {
        const char *name = argc > 0 ? argv[0] : "";
        return new AnimationEffect<LIGHT_TYPE>(name);
    };
    effectFactories[MUSIC] = [](int argc, const char *argv[]) {
        uint8_t mode = argc > 0 ? atoi(argv[0]) : 1;
        return new MusicEffect<LIGHT_TYPE>(mode);
    };
    effectFactories[CUSTOM] = [](int argc, const char *argv[]) {
        return new CustomEffect<LIGHT_TYPE>();
    };
}

void registerCommands() {
    cmdHandler.setDefaultHandler([](SenderFunc sender, int argc, char *argv[]) {
        sender("Unknown command. type 'help' for helps.");
    });
    cmdHandler.registerCommand("help", "Show command helps", [](SenderFunc sender, int argc, char *argv[]) {
        cmdHandler.printHelp(sender);
    });
#ifdef ENABLE_DEBUG
    cmdHandler.registerCommand("debug", "Show debug info", [](SenderFunc sender, int argc, char *argv[]) {
        printSystemInfo();
        printWifiInfo();
    });
#endif
    cmdHandler.registerCommand("version", "Show version", [](SenderFunc sender, int argc, char *argv[]) {
        StaticJsonDocument<256> doc;
        doc["product"] = product_name;
        doc["model"] = model_name;
        doc["id"] = ESP.getChipId();
        doc["version"] = version;
        doc["versionCode"] = version_code;
        doc["sdkVersion"] = ESP.getFullVersion();
        String str;
        serializeJson(doc, str);
        sender(str.c_str());
    });
    cmdHandler.registerCommand("status", "Show status", [](SenderFunc sender, int argc, char *argv[]) {
        StaticJsonDocument<256> doc;
        doc["vcc"] = ESP.getVcc() / 1000.0;
        doc["resetReason"] = ESP.getResetReason();
        doc["freeHeap"] = ESP.getFreeHeap();
        doc["heapFragment"] = ESP.getHeapFragmentation();
        doc["maxFreeBlock"] = ESP.getMaxFreeBlockSize();
        doc["RSSI"] = WiFi.RSSI();
        FSInfo fs_info;
        LittleFS.info(fs_info);
        doc["fsTotalSpace"] = fs_info.totalBytes;
        doc["fsUsedSpace"] = fs_info.usedBytes;
        String str;
        serializeJson(doc, str);
        sender(str.c_str());
    });
    cmdHandler.registerCommand("config", "Get config", [](SenderFunc sender, int argc, char *argv[]) {
        StaticJsonDocument<1024> doc;
        if (WiFi.getMode() == WIFI_AP) {
            struct ip_info info;
            wifi_get_ip_info(SOFTAP_IF, &info);
            doc["ssid"] = emptyString;
            doc["ip"] = IPAddress(info.ip.addr).toString();
            doc["mask"] = IPAddress(info.netmask.addr).toString();
            doc["gateway"] = IPAddress(info.gw.addr).toString();
        } else {
            doc["ssid"] = WiFi.SSID();
            doc["ip"] = WiFi.localIP().toString();
            doc["mask"] = WiFi.subnetMask().toString();
            doc["gateway"] = WiFi.gatewayIP().toString();
        }
        serializeSettings(doc, false);
        String str;
        serializeJson(doc, str);
        sender(str.c_str());
    });
    cmdHandler.registerCommand("scan", "Scan wifi", [](SenderFunc sender, int argc, char *argv[]) {
        DynamicJsonDocument doc(1024);
        JsonArray array = doc.to<JsonArray>();
        scanWifi(array);
        String str;
        serializeJson(doc, str);
        sender(str.c_str());
    });
    cmdHandler.registerCommand("connect", "Connect to wifi", [](SenderFunc sender, int argc, char *argv[]) {
        if (argc <= 1) {
            sender("INVAILD");
            return;
        }
        String ssid(argv[1]);
        String password(argc > 2 ? argv[2] : "");
        if (connectWifi(ssid, password)) {
            sender(WiFi.localIP().toString().c_str());
            setWifiMode(WIFI_STA);
            config.ssid = ssid;
            config.password = password;
            markDirty();
        } else {
            sender("ERR");
            if (WiFi.getMode() == WIFI_STA && !connectWifi(config.ssid, config.password)) {
                startHotspot();
                setWifiMode(WIFI_AP);
            }
        }
    });
    cmdHandler.registerCommand("disconnect", "Disconnect from wifi", [](SenderFunc sender, int argc, char *argv[]) {
        startHotspot();
        sender("OK");
        setWifiMode(WIFI_AP);
        config.ssid = "";
        config.password = "";
        markDirty();
    });
    cmdHandler.registerCommand("name", "Get/set device name", [](SenderFunc sender, int argc, char *argv[]) {
        if (argc <= 1) {
            String str = config.name + String(',') + config.hostname;
            sender(str.c_str());
            return;
        }
        config.name = argv[1];
        if (argc > 2) config.hostname = argv[2];
        markDirty();
        sender("OK");
        if (WiFi.getMode() == WIFI_AP) {
            startHotspot();
        }
        setWifiMode(WiFi.getMode());
    });
    cmdHandler.registerCommand("mode", "Get/set light mode", [](SenderFunc sender, int argc, char *argv[]) {
        if (argc <= 1) {
            String str = effect2str(lightEffect->type());
            sender(str.c_str());
            return;
        }
        EffectType type = str2effect(argv[1]);
        if (type >= CONSTANT && type < EFFECT_TYPE_COUNT) {
            delete lightEffect;
            lightEffect = effectFactories[type](argc - 2, (const char **) argv + 2);
            markDirty();
            sender("OK");
        } else {
            sender("INVAILD");
        }
    });
    cmdHandler.registerCommand("brightness", "Get/set brightness", [](SenderFunc sender, int argc, char *argv[]) {
        if (argc <= 1) {
            String str = String(config.brightness);
            sender(str.c_str());
            return;
        }
        int brightness = atoi(argv[1]);
        if (brightness >= 0 && brightness <= 255) {
            if (config.brightness != brightness) {
                FastLED.setBrightness(brightness);
                FastLED.show();
                config.brightness = (uint8_t) brightness;
                markDirty();
            }
            sender("OK");
        } else {
            sender("INVAILD");
        }
    });
    cmdHandler.registerCommand("temperature", "Get/set temperature", [](SenderFunc sender, int argc, char *argv[]) {
        if (argc <= 1) {
            String str = String(config.temperature) + String('K');
            sender(str.c_str());
            return;
        }
        int temperature = atoi(argv[1]);
        if (temperature >= 0) {
            if (config.temperature != temperature) {
                FastLED.setTemperature(CRGB(kelvin2rgb(temperature)));
                FastLED.show();
                config.temperature = (uint32_t) temperature;
                markDirty();
            }
            sender("OK");
        } else {
            sender("INVAILD");
        }
    });
    cmdHandler.registerCommand("fps", "Get/set refresh rate", [](SenderFunc sender, int argc, char *argv[]) {
        if (argc <= 1) {
            String str = String(config.refreshRate);
            sender(str.c_str());
            return;
        }
        int rate = atoi(argv[1]);
        if (rate > 0 && rate <= 400) {
            if (config.refreshRate != rate) {
                if (timer.active()) timer.detach();
                timer.attach_ms(1000 / rate, updateLight);
                config.refreshRate = (uint16_t) rate;
                markDirty();
            }
            sender("OK");
        } else {
            sender("INVAILD");
        }
    });
}

ADC_MODE(ADC_VCC); // Enable ESP.getVcc()

void setup() {
    FastLED.addLeds<LED_TYPE, LED_DATA_PIN, LED_COLOR_ORDER>(light.data(), light.count());
#ifdef LED_CORRECTION
    FastLED.setCorrection(CRGB(LED_CORRECTION));
#endif
#ifdef LED_MAX_POWER_MW
    // TODO FastLED 的功率限制是在刷新灯珠的时候做的, 但我需要限制的是最大亮度
    FastLED.setMaxPowerInMilliWatts(LED_MAX_POWER_MW);
#endif
    FastLED.clear(true);

    Serial.begin(115200);
    Serial.println();
    Serial.print(F("RGB Light, version: "));
    Serial.println(version);
    Serial.println(F("Made by QingChenW with love"));
#ifdef ENABLE_DEBUG
    gdbstub_init(); // XXX 在 esp8266-arduino 3.0+ 上疑似会严重干扰 LED 时序
#endif

    LittleFS.begin();
    readSettings();

    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    if (connectWifi(config.ssid, config.password)) {
        setWifiMode(WIFI_STA);
    } else {
        startHotspot();
        setWifiMode(WIFI_AP);
    }

    initEffects();
    registerCommands();

    Serial.println(F("Start HTTP server"));
    webServer.onNotFound([]() {
        // Implement Captive Portal
        webServer.sendHeader("Location", String("/"), true);
        webServer.send(302, MIME_TYPE(txt), "");
    });
    webServer.on("/version", HTTP_GET, []() {
        cmdHandler.parseCommand([](const char *msg) {
            webServer.send(200, MIME_TYPE(json), msg);
        }, "version");
    });
    webServer.on("/status", HTTP_GET, []() {
        cmdHandler.parseCommand([](const char *msg) {
            webServer.send(200, MIME_TYPE(json), msg);
        }, "status");
    });
    webServer.on("/config", HTTP_GET, []() {
        cmdHandler.parseCommand([](const char *msg) {
            webServer.send(200, MIME_TYPE(json), msg);
        }, "config");
    });
    static auto checkPath = [](const String &path) {
        if (path.isEmpty()) {
            webServer.send(400, MIME_TYPE(txt), PSTR("Bad request"));
            return false;
        }
        if (!LittleFS.exists(path)) {
            webServer.send(404, MIME_TYPE(txt), PSTR("Not found"));
            return false;
        }
        return true;
    };
    webServer.on("/list", HTTP_GET, []() {
        String path = webServer.arg("path");
        if (!checkPath(path)) {
            return;
        }
        DynamicJsonDocument doc(1024);
        JsonArray array = doc.to<JsonArray>();
        Dir dir = LittleFS.openDir(path);
        while (dir.next()) {
            JsonObject obj = array.createNestedObject();
            obj["name"] = dir.fileName();
            obj["size"] = dir.fileSize();
            obj["isDir"] = dir.isDirectory();
        }
        String str;
        serializeJson(doc, str);
        webServer.send(200, MIME_TYPE(json), str);
    });
    webServer.on("/download", HTTP_GET, []() {
        String path = webServer.arg("path");
        if (!checkPath(path)) {
            return;
        }
        File file = LittleFS.open(path, "r");
        Serial.printf_P(PSTR("Download file: %s\n"), file.name());
        webServer.streamFile(file, FPSTR(MIME_TYPE(none)));
        file.close();
    });
    webServer.on("/upload", HTTP_POST, []() {
        webServer.send(200, MIME_TYPE(txt), PSTR("OK"));
    }, []() {
        static File uploadFile;
        HTTPUpload &upload = webServer.upload();
        if (upload.status == UPLOAD_FILE_START) {
            String path = webServer.arg("path") + "/" + upload.filename;
            uploadFile = LittleFS.open(path, "w");
            if (!uploadFile) {
                webServer.send(500, MIME_TYPE(txt), PSTR("Internal server error"));
                return;
            }
            Serial.printf_P(PSTR("Upload started, file: %s\n"), path.c_str());
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (uploadFile) {
                if (uploadFile.write(upload.buf, upload.currentSize) != upload.currentSize) {
                    webServer.send(500, MIME_TYPE(txt), PSTR("Internal server error"));
                    return;
                }
            }
            Serial.printf_P(PSTR("Uploading, size: %u\n"), upload.currentSize);
        } else if (upload.status == UPLOAD_FILE_END) {
            if (uploadFile) {
                uploadFile.close();
            }
            Serial.printf_P(PSTR("Upload finished, size: %u\n"), upload.totalSize);
        }
        yield();
    });
    webServer.on("/delete", HTTP_GET, []() {
        String path = webServer.arg("path");
        if (!checkPath(path)) {
            return;
        }
        if (LittleFS.remove(path)) {
            webServer.send(200, MIME_TYPE(txt), PSTR("OK"));
        } else {
            webServer.send(500, MIME_TYPE(txt), PSTR("Internal server error"));
        }
    });
    webServer.on("/upgrade", HTTP_GET, []() {
        String path = webServer.arg("path");
        if (!checkPath(path)) {
            return;
        }
        bool success = false;
        File file = LittleFS.open(path, "r");
        if (file) {
            if (Update.begin(file.size())) { // Update 能升级文件系统, 但为了避免刷掉用户的动画文件, 我决定不用
                Serial.println(F("Start to update"));
                uint32_t writtenSize = Update.writeStream(file);
                if (Update.end(true)) {
                    Serial.printf_P(PSTR("Update success, size: %u\n"), writtenSize);
                    success = true;
                } else {
                    Update.printError(Serial);
                }
            } else {
                Update.printError(Serial);
            }
            file.close();
        } else {
            Serial.println(F("Open OTA file failed"));
        }
        if (success) {
            LittleFS.remove(path);
            webServer.sendHeader("Connection", "close");
            webServer.send(200, MIME_TYPE(txt), PSTR("OK"));
            delay(2000);
            Serial.println(F("Rebooting..."));
            ESP.restart();
        } else {
            webServer.send(500, MIME_TYPE(txt), PSTR("Internal server error"));
        }
    });
    webServer.serveStatic("/", LittleFS, "/www/", "max-age=300");
    webServer.begin();
    Serial.println(F("Start WebSocket server"));
    wsServer.onEvent([](uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
        switch (type) {
            case WStype_CONNECTED: { // payload is "/"
                IPAddress ip = wsServer.remoteIP(num);
                Serial.printf_P(PSTR("Client %u connected from %s\n"), num, ip.toString().c_str());
                break;
            }
            case WStype_DISCONNECTED: {
                Serial.printf_P(PSTR("Client %u disconnected\n"), num);
                break;
            }
            case WStype_TEXT: {
                char *str = (char*) payload;
                if (length > 0) {
#ifdef ENABLE_DEBUG
                    Serial.printf("Received message from ws%u: %s\n", num, str);
#endif
                    handleCommand([num](const char *msg) {
                        wsServer.sendTXT(num, msg, strlen(msg));
                    }, str);
                    yield();
                }
                break;
            }
        }
    });
    wsServer.begin();
}

void loop() {
    if (config.isDirty && millis() - config.lastModifyTime >= CONFIG_SAVE_PERIOD) {
        saveSettings();
    }
    while (Serial.available() > 0) {
        char buffer[128];
        size_t len = Serial.readBytesUntil('\n', buffer, sizeof(buffer) - 1);
        if (len > 0) {
            buffer[len] = '\0';
#ifdef ENABLE_DEBUG
            Serial.printf("Received data from com: %s\n", buffer);
#endif
            handleCommand([](const char *msg) {
                Serial.println(msg);
            }, buffer);
            yield();
        }
    }
    dnsServer.processNextRequest();
    webServer.handleClient();
    wsServer.loop();
    MDNS.update();
}

#ifdef ENABLE_DEBUG
void printSystemInfo() {
    Serial.println("----- System Information -----");
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

void printWifiInfo() {
    Serial.println("----- WIFI Information -----");
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
#endif
