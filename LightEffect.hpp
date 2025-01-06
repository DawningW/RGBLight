#ifndef __LIGHTEFFECT_HPP__
#define __LIGHTEFFECT_HPP__

#include <Arduino.h>
#include <LittleFS.h>
#include <FastLED.h>
#include <ArduinoJson.h>

#include "Light.hpp"
#include "utils.h"

enum EffectType {
    CONSTANT,    // 常亮
    BLINK,       // 闪烁
    BREATH,      // 呼吸
    CHASE,       // 跑马灯
    RAINBOW,     // 彩虹
    STREAM,      // 流光
    ANIMATION,   // 动画
    MUSIC,       // 音乐律动
    CUSTOM,      // 上位机控制
    EFFECT_TYPE_COUNT
};

/**
 * @brief Get light effect enum from name
 * 
 * @param str effect name
 * @return EffectType effect enum
 */
EffectType str2effect(const char *str);

/**
 * @brief Get name from light effect enum
 * 
 * @param effect effect enum
 * @return const char* effect name
 */
const char* effect2str(EffectType effect);

extern const uint16_t &fps;

class Effect {
public:
    virtual EffectType type() = 0;
    virtual bool update(Light &light, uint32_t deltaTime) = 0;
    virtual void writeToJSON(JsonDocument &json) { json["mode"] = type(); };
    template <typename LIGHT>
    static Effect* readFromJSON(JsonDocument &json);
};

template <typename LIGHT>
class ConstantEffect : public Effect {
private:
    bool updated;
    CRGB currentColor;

public:
    ConstantEffect(uint32_t color) :
        updated(false), currentColor(color) {}

    EffectType type() override {
        return CONSTANT;
    }

    bool update(Light &light, uint32_t deltaTime) override {
        if (!updated) {
            fill_solid(light.data(), light.count(), currentColor);
            updated = true;
            return true;
        }
        return false;
    }

    void writeToJSON(JsonDocument &json) override {
        Effect::writeToJSON(json);
        json["color"] = rgb2hex(currentColor.r, currentColor.g, currentColor.b);
    }

    static ConstantEffect* readFromJSON(JsonDocument &json) {
        uint32_t color = json["color"];
        return new ConstantEffect(color);
    }
};

template <typename LIGHT>
class BlinkEffect : public Effect {
private:
    uint16_t currentFrame;
    CRGB currentColor;
    float lastTime;
    float interval;

public:
    BlinkEffect(uint32_t color, float lastTime, float interval) :
        currentFrame(0), currentColor(color), lastTime(lastTime), interval(interval) {}

    EffectType type() override {
        return BLINK;
    }

    bool update(Light &light, uint32_t deltaTime) override {
        int lastTime = fps * this->lastTime;
        int interval = fps * this->interval;
        bool needUpdate = false;
        if (currentFrame == 0) {
            fill_solid(light.data(), light.count(), currentColor);
            needUpdate = true;
        } else if (currentFrame == lastTime) {
            fill_solid(light.data(), light.count(), CRGB::Black);
            needUpdate = true;
        }
        if (++currentFrame >= lastTime + interval) {
            currentFrame = 0;
        }
        return needUpdate;
    }

    void writeToJSON(JsonDocument &json) override {
        Effect::writeToJSON(json);
        json["color"] = rgb2hex(currentColor.r, currentColor.g, currentColor.b);
        json["lastTime"] = lastTime;
        json["interval"] = interval;
    }

    static BlinkEffect* readFromJSON(JsonDocument &json) {
        uint32_t color = json["color"];
        float lastTime = json["lastTime"];
        float interval = json["interval"];
        return new BlinkEffect(color, lastTime, interval);
    }
};

template <typename LIGHT>
class BreathEffect : public Effect {
private:
    uint16_t currentFrame;
    CRGB currentColor;
    float lastTime;
    float interval;

public:
    BreathEffect(uint32_t color, float lastTime, float interval) :
        currentFrame(0), currentColor(color), lastTime(lastTime), interval(interval) {}

    EffectType type() override {
        return BREATH;
    }

    bool update(Light &light, uint32_t deltaTime) override {
        int lastTime = fps * this->lastTime;
        int interval = fps * this->interval;
        bool needUpdate = false;
        if (currentFrame <= lastTime) {
            CRGB rgb = currentColor;
            double x = (double) currentFrame / lastTime;
            int scale = -1010 * x * x + 1010 * x;
            rgb.nscale8(scale);
            fill_solid(light.data(), light.count(), rgb);
            needUpdate = true;
        }
        if (++currentFrame >= lastTime + interval) {
            currentFrame = 0;
        }
        return needUpdate;
    }

    void writeToJSON(JsonDocument &json) override {
        Effect::writeToJSON(json);
        json["color"] = rgb2hex(currentColor.r, currentColor.g, currentColor.b);
        json["lastTime"] = lastTime;
        json["interval"] = interval;
    }

    static BreathEffect* readFromJSON(JsonDocument &json) {
        uint32_t color = json["color"];
        float lastTime = json["lastTime"];
        float interval = json["interval"];
        return new BreathEffect(color, lastTime, interval);
    }
};

template <typename LIGHT>
class ChaseEffect : public Effect {
private:
    uint16_t currentFrame;
    CRGB currentColor;
    uint8_t direction;
    float lastTime;

public:
    ChaseEffect(uint32_t color, uint8_t direction, float lastTime) :
        currentFrame(0), currentColor(color), direction(direction), lastTime(lastTime) {}

    EffectType type() override {
        return CHASE;
    }

    bool update(Light &light, uint32_t deltaTime) override {
        return update(static_cast<LIGHT&>(light), deltaTime);
    }

    template <int COUNT, bool REVERSE>
    bool update(LightStrip<COUNT, REVERSE> &light, uint32_t deltaTime) {
        int lastTime = fps * this->lastTime;
        bool needUpdate = false;
        if (currentFrame % lastTime == 0) {
            fill_solid(light.data(), light.count(), CRGB::Black);
            int index = currentFrame / lastTime;
            if (index > light.l() * 2 - 1) {
                currentFrame = 0;
                index = 0;
            } else if (index > light.l() - 1) {
                index = light.l() * 2 - 1 - index;
            }
            light.at(index) = currentColor;
            needUpdate = true;
        }
        ++currentFrame;
        return needUpdate;
    }

    template <int ARRANGEMENT, int... COUNT_PER_RING>
    bool update(LightDisc<ARRANGEMENT, COUNT_PER_RING...> &light, uint32_t deltaTime) {
        int lastTime = fps * this->lastTime;
        bool needUpdate = false;
        if (currentFrame % lastTime == 0) {
            fill_solid(light.data(), light.count(), CRGB::Black);
            int index = currentFrame / lastTime;
            if (index > light.r() * 2 - 1) {
                currentFrame = 0;
                index = 0;
            } else if (index > light.r() - 1) {
                index = light.r() * 2 - 1 - index;
            }
            for (int j = 0; j < light.l(index); j++) {
                light.at(index, j) = currentColor;
            }
            needUpdate = true;
        }
        ++currentFrame;
        return needUpdate;
    }

    void writeToJSON(JsonDocument &json) override {
        Effect::writeToJSON(json);
        json["color"] = rgb2hex(currentColor.r, currentColor.g, currentColor.b);
        json["direction"] = direction;
        json["lastTime"] = lastTime;
    }

    static ChaseEffect* readFromJSON(JsonDocument &json) {
        uint32_t color = json["color"];
        uint8_t direction = json["direction"];
        float lastTime = json["lastTime"];
        return new ChaseEffect(color, direction, lastTime);
    }
};

template <typename LIGHT>
class RainbowEffect : public Effect {
private:
    uint8_t currentHue;
    int8_t delta;

public:
    RainbowEffect(int8_t delta) :
        currentHue(0), delta(delta) {}

    EffectType type() override {
        return RAINBOW;
    }

    bool update(Light &light, uint32_t deltaTime) override {
        CHSV hsv(currentHue, 255, 240);
        CRGB rgb;
        hsv2rgb_rainbow(hsv, rgb);
        fill_solid(light.data(), light.count(), rgb);
        currentHue += delta;
        return true;
    }

    void writeToJSON(JsonDocument &json) override {
        Effect::writeToJSON(json);
        json["delta"] = delta;
    }

    static RainbowEffect* readFromJSON(JsonDocument &json) {
        uint8_t delta = json["delta"];
        return new RainbowEffect(delta);
    }
};

template <typename LIGHT>
class StreamEffect : public Effect {
private:
    uint8_t currentHue;
    uint8_t direction;
    int8_t delta;

public:
    StreamEffect(uint8_t direction, int8_t delta) :
        currentHue(0), direction(direction), delta(delta) {}

    EffectType type() override {
        return STREAM;
    }

    bool update(Light &light, uint32_t deltaTime) override {
        return update(static_cast<LIGHT&>(light), deltaTime);
    }

    template <int COUNT, bool REVERSE>
    bool update(LightStrip<COUNT, REVERSE> &light, uint32_t deltaTime) {
        fill_rainbow(light.data(), light.count(), currentHue);
        currentHue += delta;
        return true;
    }

    template <int ARRANGEMENT, int... COUNT_PER_RING>
    bool update(LightDisc<ARRANGEMENT, COUNT_PER_RING...> &light, uint32_t deltaTime) {
        CRGB rgb[light.r()];
        fill_rainbow(rgb, light.r(), currentHue);
        for (int i = 0; i < light.r(); i++) {
            for (int j = 0; j < light.l(i); j++) {
                light.at(i, j) = rgb[i];
            }
        }
        currentHue += delta;
        return true;
    }

    void writeToJSON(JsonDocument &json) override {
        Effect::writeToJSON(json);
        json["direction"] = direction;
        json["delta"] = delta;
    }

    static StreamEffect* readFromJSON(JsonDocument &json) {
        uint8_t direction = json["direction"];
        uint8_t delta = json["delta"];
        return new StreamEffect(direction, delta);
    }
};

template <typename LIGHT>
class AnimationEffect : public Effect {
private:
    String animName;
    File file;
    uint16_t currentFrame;

public:
    AnimationEffect(const char *animName) :
        animName(animName), currentFrame(0) {
        if (strlen(animName) > 0) {
            String path = String("/animations/") + animName;
            file = LittleFS.open(path, "r");
#if defined(ESP8266)
            if (!file.isFile()) {
#elif defined(ESP32)
            if (file.isDirectory()) {
#endif
                file.close();
            }
        }
        if (file) {
            Serial.print(F("Start to play animation: "));
        } else {
            Serial.print(F("Failed to open animation: "));
        }
        Serial.println(animName);
    }

    ~AnimationEffect() {
        if (file) {
            file.close();
            Serial.println(F("Stop playing animation"));
        }
    }

    EffectType type() override {
        return ANIMATION;
    }

    bool update(Light &light, uint32_t deltaTime) override {
        if (!file || file.size() == 0) {
            return false;
        }
#ifdef ENABLE_DEBUG
        Serial.printf_P(PSTR("Playing anim frame: %d\n"), currentFrame);
#endif
        int index = 0;
        while (index < light.count()) {
            CRGB &pixel = light.data()[index];
            if (file.read(pixel.raw, sizeof(pixel.raw)) != sizeof(pixel.raw)) {
                Serial.println(F("End of animation, replay"));
                file.seek(0);
                currentFrame = 0;
                continue;
            }
            index++;
        }
        currentFrame++;
        return true;
    }

    void writeToJSON(JsonDocument &json) override {
        Effect::writeToJSON(json);
        json["animName"] = animName;
    }

    static AnimationEffect* readFromJSON(JsonDocument &json) {
        const char *animName = json["animName"];
        return new AnimationEffect(animName);
    }
};

template <typename LIGHT>
class MusicEffect : public Effect {
private:
    uint8_t soundMode; // 0-电平模式 1-频谱模式
    uint8_t currentHue;
    double currentVolume; // Must be 0~1

public:
    MusicEffect(uint8_t mode) :
        soundMode(mode), currentHue(0), currentVolume(0.0) {}

    void setVolume(double volume) {
        currentVolume = volume;
    }

    EffectType type() override {
        return MUSIC;
    }

    bool update(Light &light, uint32_t deltaTime) override {
        return update(static_cast<LIGHT&>(light), deltaTime);
    }

    template <int COUNT, bool REVERSE>
    bool update(LightStrip<COUNT, REVERSE> &light, uint32_t deltaTime) {
        if (soundMode == 0) {
            int count = light.l() * currentVolume;
            fill_solid(light.data(), light.count(), CRGB::Black);
            if (count > 0) {
                fill_solid(light.data(), count - 1, CRGB::Green);
                light.at(count - 1) = CRGB::Red;
            }
        } else {
            int count = light.l() * currentVolume;
            CHSV hsv(currentHue++, 255, 240);
            CRGB rgb;
            hsv2rgb_rainbow(hsv, rgb);
            fill_solid(light.data(), light.count(), CRGB::Black);
            fill_solid(light.data() + (light.count() - count) / 2, count, rgb);
        }
        return true;
    }

    template <int ARRANGEMENT, int... COUNT_PER_RING>
    bool update(LightDisc<ARRANGEMENT, COUNT_PER_RING...> &light, uint32_t deltaTime) {
        if (soundMode == 0) {
            int r = light.r() * currentVolume;
            fill_solid(light.data(), light.count(), CRGB::Black);
            if (r > 0) {
                for (int i = 0; i < r; i++) {
                    for (int j = 0; j < light.l(i); j++) {
                        light.at(i, j) = CRGB::Green;
                    }
                }
                int l = light.l(r - 1);
                for (int j = 0; j < l; j++) {
                    light.at(r - 1, j) = CRGB::Red;
                }
            }
        } else {
            int r = ceil(light.r() * currentVolume);
            CHSV hsv(currentHue++, 255, 240);
            CRGB rgb;
            hsv2rgb_rainbow(hsv, rgb);
            fill_solid(light.data(), light.count(), CRGB::Black);
            for (int i = light.r() - r; i < light.r(); i++) {
                CRGB temp = rgb;
                if (i == light.r() - r) {
                    temp.nscale8_video(255 * (currentVolume - floor(light.r() * currentVolume) / light.r()) * light.r());
                }
                for (int j = 0; j < light.l(i); j++) {
                    light.at(i, j) = temp;
                }
            }
        }
        return true;
    }

    void writeToJSON(JsonDocument &json) override {
        Effect::writeToJSON(json);
        json["soundMode"] = soundMode;
    }

    static MusicEffect* readFromJSON(JsonDocument &json) {
        uint8_t soundMode = json["soundMode"];
        return new MusicEffect(soundMode);
    }
};

template <typename LIGHT>
class CustomEffect : public Effect {
private:
    int index;

public:
    CustomEffect() : index(0) {}

    int& getIndex() {
        return index;
    }

    EffectType type() override {
        return CUSTOM;
    }

    bool update(Light &light, uint32_t deltaTime) override {
        return true;
    }

    void writeToJSON(JsonDocument &json) override {
        Effect::writeToJSON(json);
    }

    static CustomEffect* readFromJSON(JsonDocument &json) {
        return new CustomEffect();
    }
};

template <typename LIGHT>
Effect* Effect::readFromJSON(JsonDocument &json) {
    if (json.containsKey("mode")) {
        EffectType mode = json["mode"].as<EffectType>();
        switch (mode) {
            case CONSTANT:
                return ConstantEffect<LIGHT>::readFromJSON(json);
            case BLINK:
                return BlinkEffect<LIGHT>::readFromJSON(json);
            case BREATH:
                return BreathEffect<LIGHT>::readFromJSON(json);
            case CHASE:
                return ChaseEffect<LIGHT>::readFromJSON(json);
            case RAINBOW:
                return RainbowEffect<LIGHT>::readFromJSON(json);
            case STREAM:
                return StreamEffect<LIGHT>::readFromJSON(json);
            case ANIMATION:
                return AnimationEffect<LIGHT>::readFromJSON(json);
            case MUSIC:
                return MusicEffect<LIGHT>::readFromJSON(json);
            case CUSTOM:
                return CustomEffect<LIGHT>::readFromJSON(json);
        }
    }
    return new ConstantEffect<LIGHT>(DEFAULT_COLOR); // 默认为常亮
}

#endif // __LIGHTEFFECT_HPP__
