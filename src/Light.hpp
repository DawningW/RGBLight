#ifndef __LIGHT_HPP__
#define __LIGHT_HPP__

#include <Arduino.h>
#include <FastLED.h>

#include "config.h"
#include "utils.h"

class Light {
public:
    virtual CRGB *data() = 0;
    virtual int count() = 0;
};

template <int COUNT, bool REVERSE>
class LightStrip : public Light {
private:
    CRGB leds[COUNT];

public:
    CRGB *data() override {
        return leds;
    }

    int count() override {
        return COUNT;
    }

    int l() {
        return COUNT;
    }

    CRGB &at(int i) {
        if (REVERSE) {
            return leds[l() - i - 1];
        } else {
            return leds[i];
        }
    }
};

enum LightPanelArrangement {
    Z_WORD     = 0x0, // Z 字形
    SNAKE      = 0x1, // 蛇形
    HORIZONTAL = 0x0, // 水平
    VERTICAL   = 0x2, // 垂直
    MIRROR     = 0x4, // 镜像
    FLIP       = 0x8, // 翻转
};

template <int X_COUNT, int Y_COUNT, int ARRANGEMENT>
class LightPanel : public Light {
private:
    CRGB leds[X_COUNT * Y_COUNT];

public:
    CRGB *data() override {
        return leds;
    }

    int count() override {
        return X_COUNT * Y_COUNT;
    }

    int w() {
        return X_COUNT;
    }

    int h() {
        return Y_COUNT;
    }

    CRGB &at(int x, int y) {
        int _w = w();
        int _h = h();
        if (ARRANGEMENT & VERTICAL) {
            std::swap(x, y);
            std::swap(_w, _h);
        }
        if (ARRANGEMENT & SNAKE) {
            if (y % 2 == 1) {
                x = _w - x - 1;
            }
        }
        if (ARRANGEMENT & MIRROR) {
            x = _w - x - 1;
        }
        if (ARRANGEMENT & FLIP) {
            y = _h - y - 1;
        }
        return leds[y * _w + x];
    }
};

enum LightDiscArrangement {
    CLOCKWISE     = 0x0, // 顺时针
    ANTICLOCKWISE = 0x1, // 逆时针
    OUTSIDE_IN    = 0x0, // 从外到内
    INSIDE_OUT    = 0x2, // 从内到外
};

template <int ARRANGEMENT, int... COUNT_PER_RING>
class LightDisc : public Light {
private:
    static constexpr int led_ring_count = sizeof...(COUNT_PER_RING);
    static constexpr int led_rings[led_ring_count] = {COUNT_PER_RING...};
    static constexpr int led_count = sum(led_rings, led_rings + led_ring_count, 0);
    const int rings[led_ring_count] = {COUNT_PER_RING...};
    CRGB leds[led_count];

public:
    CRGB *data() override {
        return leds;
    }

    int count() override {
        return led_count;
    }

    int r() {
        return led_ring_count;
    }

    int l(int ring) {
        return rings[ring];
    }

    CRGB &at(int ring, int i) {
        if (ARRANGEMENT & ANTICLOCKWISE) {
            i = l(ring) - i - 1;
        }
        if (ARRANGEMENT & INSIDE_OUT) {
            return leds[sum(rings + ring + 1, rings + r(), i)];
        } else {
            return leds[sum(rings, rings + ring, i)];
        }
    }
};

template <int X_COUNT, int Y_COUNT, int Z_COUNT>
class LightCube : public Light {
private:
    CRGB leds[X_COUNT * Y_COUNT * Z_COUNT];

public:
    CRGB *data() override {
        return leds;
    }

    int count() override {
        return X_COUNT * Y_COUNT * Z_COUNT;
    }

    int l() {
        return X_COUNT;
    }

    int w() {
        return Y_COUNT;
    }

    int h() {
        return Z_COUNT;
    }

    CRGB &at(int x, int y, int z) {
        return leds[z * l() * w() + y * l() + x];
    }
};

#endif // __LIGHT_HPP__
