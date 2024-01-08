#ifndef __LIGHT_HPP__
#define __LIGHT_HPP__

#include <Arduino.h>
#include <FastLED.h>

#include "config.h"
#include "utils.h"

// ==================== LightStrip ====================

template <int COUNT, bool REVERSE>
class LightStrip {
public:
    static constexpr int count() {
        return COUNT;
    }

private:
    CRGB leds[count()];
public:
    CRGB* data() {
        return this->leds;
    }

public:
    constexpr int l() const {
        return COUNT;
    }

    CRGB& at(int i) {
        if (REVERSE) {
            return this->leds[l() - i - 1];
        } else {
            return this->leds[i];
        }
    }
};

// ==================== LightPanel ====================

enum LightPanelArrangement {
    Z_WORD     = 0x0, // Z 字形
    SNAKE      = 0x1, // 蛇形
    HORIZONTAL = 0x0, // 水平
    VERTICAL   = 0x2, // 垂直
    MIRROR     = 0x4, // 镜像
    FLIP       = 0x8, // 翻转
};

template <int X_COUNT, int Y_COUNT, int ARRANGEMENT>
class LightPanel {
public:
    static constexpr int count() {
        return X_COUNT * Y_COUNT;
    }

private:
    CRGB leds[count()];
public:
    CRGB* data() {
        return this->leds;
    }

public:
    constexpr int w() const {
        return X_COUNT;
    }

    constexpr int h() const {
        return Y_COUNT;
    }

    CRGB& at(int x, int y) {
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
        return this->leds[y * _w + x];
    }
};

// ==================== LightDisc ====================

enum LightDiscArrangement {
    CLOCKWISE     = 0x0, // 顺时针
    ANTICLOCKWISE = 0x1, // 逆时针
    OUTSIDE_IN    = 0x0, // 从外到内
    INSIDE_OUT    = 0x2, // 从内到外
};

template <int ARRANGEMENT, int... COUNT_PER_RING>
class LightDisc {
private:
    static constexpr int ring_count() {
        return sizeof...(COUNT_PER_RING);
    }
    static constexpr int rings[ring_count()] = {COUNT_PER_RING...};

public:
    static constexpr int count() {
        return sum(rings, rings + ring_count(), 0);
    }

private:
    CRGB leds[count()];
public:
    CRGB* data() {
        return this->leds;
    }

public:
    constexpr int r() const {
        return ring_count();
    }

    constexpr int l(int ring) const {
        return rings[ring];
    }

    CRGB& at(int ring, int i) {
        if (ARRANGEMENT & ANTICLOCKWISE) {
            i = l(ring) - i - 1;
        }
        if (ARRANGEMENT & INSIDE_OUT) {
            return this->leds[sum(rings + ring + 1, rings + r(), 0) + i];
        } else {
            return this->leds[sum(rings, rings + ring, 0) + i];
        }
    }
};

template <int ARRANGEMENT, int... COUNT_PER_RING>
constexpr int LightDisc<ARRANGEMENT, COUNT_PER_RING...>::rings[];

// ==================== LightCube ====================

template <int X_COUNT, int Y_COUNT, int Z_COUNT>
class LightCube {
public:
    static constexpr int count() {
        return X_COUNT * Y_COUNT * Z_COUNT;
    }

private:
    CRGB leds[count()];
public:
    CRGB* data() { return this->leds; }

public:
    constexpr int l() const {
        return X_COUNT;
    }

    constexpr int w() const {
        return Y_COUNT;
    }

    constexpr int h() const {
        return Z_COUNT;
    }

    CRGB& at(int x, int y, int z) {
        return this->leds[z * l() * w() + y * l() + x];
    }
};

#endif // __LIGHT_HPP__
