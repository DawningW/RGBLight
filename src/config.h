#ifndef __CONFIG_H__
#define __CONFIG_H__

// 型号, 用于检查更新, 如果你使用其他开发板请修改型号, 避免被小程序推送更新
#define MODEL "NodeMCU_LEDStrip1m" // 1 米灯带版本 (30 个 WS2812B 灯珠)
// #define MODEL "ESP32C3_16x16Matrix" // 16 * 16 点阵版本
// #define MODEL "D1_mini_WCLightPanel" // 投影灯版本, 原理图详见 pcb 文件夹
// #define MODEL "ESP32S3_LightCube" // 8 * 8 * 8 光立方版本 (WIP)
// 版本号
#define VERSION "V0.4.0"
// 版本代码, 用于检查更新
#define VERSION_CODE 5

/****************************** 硬件配置 ******************************/
// LED 灯数据引脚
#define LED_DATA_PIN 4 // D2 GPIO4
// LED 灯型号, 详见 FastLED 文档
#define LED_TYPE WS2812B
// LED 灯颜色顺序, 详见 FastLED 文档
#define LED_COLOR_ORDER GRB
// LED 灯颜色校正(可选), 详见 FastLED 文档
// #define LED_CORRECTION 0xFFFFFF
// LED 灯功率限制(可选), 详见 FastLED 文档
#define LED_MAX_POWER_MW 2500
// LED 灯形态, 详见 Light.hpp
#define LIGHT_TYPE LightStrip<30, false>
// #define LIGHT_TYPE LightPanel<16, 16, SNAKE | HORIZONTAL>
// #define LIGHT_TYPE LightDisc<CLOCKWISE | OUTSIDE_IN, 12, 6, 3>

/****************************** 软件配置 ******************************/
// 开启调试模式
// #define ENABLE_DEBUG
// 给你的炫酷小彩灯起个名字吧, 会影响 WIFI 热点名称和网页/小程序的显示
#define NAME "RGBLight"
// 多少毫秒不修改配置后保存配置, 0 为每次修改后立刻保存 (建议不要设为 0, 会大大缩短 Flash 寿命)
#define CONFIG_SAVE_PERIOD (10 * 1000)

// 恭喜你, 已经完成了所有配置, 其余配置可通过网页或小程序修改, 详见 README.md

#endif // __CONFIG_H__
