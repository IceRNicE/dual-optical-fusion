#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "screen.hpp"

// ================= 全屏画布（调试用帧缓冲） =================
// 所有显示模式先把画面画到这块 320x240 的 Sprite（PSRAM），再一次性推屏。
// 好处：
//   1) 帧缓冲天然是"一整帧"，调试时可以直接抓下来发到电脑看；
//   2) 一次性推屏，消除逐行刷新带来的撕裂；
//   3) 后面要加 WiFi 推流，也只是从同一块 buffer 读。
//
// 字节序说明：设 _swapBytes = true，写入时预交换成"大端存储"，
// pushSprite 内部会再交换一次后发出，最终 SPI 上的字节与原来
// tft.pushImage(...) 直接推屏完全一致（也就是当前屏上显示正确的配置）。

static const int CANVAS_W = 320;
static const int CANVAS_H = 240;

static TFT_eSprite* g_canvas = nullptr;

// 创建画布。放在 screen_init() 之后调用。
inline bool canvas_init() {
    if (g_canvas) return true;
    g_canvas = new TFT_eSprite(&tft);
    if (!g_canvas) {
        Serial.println("[Canvas] new TFT_eSprite failed");
        return false;
    }
    if (!g_canvas->createSprite(CANVAS_W, CANVAS_H)) {
        Serial.println("[Canvas] createSprite failed (PSRAM?)");
        delete g_canvas;
        g_canvas = nullptr;
        return false;
    }
    g_canvas->setSwapBytes(true);
    // 重要：TFT_eSprite 的构造函数不会初始化文字相关状态（textfont / gfxFont 等），
    // 直接 printf 会走进 GFX 字体分支并访问未初始化的 gfxFont 指针，导致
    // Guru Meditation (LoadProhibited)。这里显式给一套安全默认值。
    g_canvas->setTextFont(1);
    g_canvas->setTextSize(1);
    g_canvas->setTextColor(TFT_WHITE, TFT_BLACK);
    g_canvas->setTextWrap(false, false);
    g_canvas->setCursor(0, 0);
    g_canvas->fillSprite(TFT_BLACK);
    Serial.printf("[Canvas] ready %dx%d, %u bytes\n",
                  CANVAS_W, CANVAS_H, (unsigned)(CANVAS_W * CANVAS_H * 2));
    return true;
}

// 把整帧推到屏幕
inline void present_canvas() {
    if (!g_canvas) return;
    g_canvas->pushSprite(0, 0);
}

// 画布像素指针（RGB565，大端存储，见上面的说明）
inline uint16_t* canvas_pixels() {
    return g_canvas ? (uint16_t*)g_canvas->getPointer() : nullptr;
}
