#ifndef __UTIL_H__
#define __UTIL_H__

#include <algorithm>
#include <numeric>
#include <type_traits>
#include <Arduino.h>

#define DEFAULT_COLOR 0xFFFFFF

#define ARRAY_LENGTH(arr) (sizeof(arr) / sizeof(arr[0]))

/**
 * @brief Convert RGB (R, G, B) to hex (0xRRGGBB)
 * 
 * @param r R component of RGB (0-255)
 * @param g G component of RGB (0-255)
 * @param b B component of RGB (0-255)
 * @return uint32_t hex value of color
 */
uint32_t rgb2hex(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Convert RGB string (#RRGGBB) to hex (0xRRGGBB)
 * 
 * @param str RGB string
 * @return uint32_t hex value of color
 */
uint32_t str2hex(const char *str);

/**
 * @brief Convert hex (0xRRGGBB) to RGB string (#RRGGBB)
 * 
 * @param hex hex value of color
 * @param str RGB string
 */
void hex2str(uint32_t hex, char *str);

/**
 * @brief Convert Kelvin (K) to hex (0xRRGGBB)
 * 
 * @param t temperature in Kelvin of color
 * @return uint32_t hex value of color
 */
uint32_t kelvin2rgb(uint32_t t);

// The compiler of ESP8266 does not support C++20...
// Older compiler even does not support C++14
template <typename T>
constexpr T sum(T *begin, T *end, typename std::decay<T>::type init) {
    return begin == end ? init : sum(begin + 1, end, init + *begin);
}

#endif // __UTIL_H__
