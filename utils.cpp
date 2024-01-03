#include "utils.h"

#include "LightEffect.hpp"

const char* EFFECT_TYPE_MAP[] = {
    "constant", "blink", "breath", "chase", "rainbow", "stream",
    "animation", "music", "custom"
};
static_assert(ARRAY_LENGTH(EFFECT_TYPE_MAP) == EFFECT_TYPE_COUNT,
                "EFFECT_TYPE_MAP size mismatch!");

uint32_t rgb2hex(uint8_t r, uint8_t g, uint8_t b) {   
    return ((r & 0xff) << 16) + ((g & 0xff) << 8) + (b & 0xff);
}

uint32_t str2hex(const char *str) {
    if (str[0] != '#')
        return DEFAULT_COLOR;
    return (uint32_t) strtol(str + 1, NULL, 16);
}

void hex2str(uint32_t hex, char *str) {
    sprintf(str, "#%06x", hex);
}

// From http://www.tannerhelland.com/4435/convert-temperature-rgb-algorithm-code/
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

EffectType str2effect(const char *str) {
    for (int i = 0; i < EFFECT_TYPE_COUNT; i++) {
        if (strcmp(str, EFFECT_TYPE_MAP[i]) == 0) {
            return (EffectType) i;
        }
    }
    return EFFECT_TYPE_COUNT;
}

const char* effect2str(EffectType effect) {
    if (effect >= EFFECT_TYPE_COUNT)
        return "";
    return EFFECT_TYPE_MAP[effect];
}
