#pragma once

#include <Arduino.h>
#include "screen.hpp"
#include "shared_val.hpp"
#include "color_map.hpp"
#include "BilinearInterpolation.hpp"
#include "camera.hpp"
#include "mlx_drivers/mlx_probe.hpp"
#include <TJpg_Decoder.h>

// ================= 融合模式渲染器 =================

// JPEG 解码回调 - 用于摄像头图像
static uint16_t* camera_frame_buffer = nullptr;  // PSRAM 中的摄像头帧缓冲区
static bool camera_frame_ready = false;

// 临时缓冲区用于画中画渲染
static uint16_t pip_thermal_buffer[PIP_WIDTH * PIP_HEIGHT];

// 全屏 Sprite（在 PSRAM 中创建）用于 alpha 融合
static TFT_eSprite* fusion_sprite = nullptr;

// 画中画模式用的全屏 Sprite（在 PSRAM 中创建）
static TFT_eSprite* pip_sprite = nullptr;

// JPEG 解码回调函数 - 将解码后的图像存入缓冲区
// 使用静态指针，这样可以在不修改回调签名的情况下访问 buffer
static uint16_t* decode_target_buffer = nullptr;
static int decode_buffer_width = 320;
static int decode_buffer_height = 240;

// 绘制最高温追踪十字（融合模式专用，坐标基于热成像区域，带布局偏移）
static inline void draw_trace_cross_fusion(int offset_x, int offset_y) {
    if (!flag_trace_max || !flag_sensor_ok) return;
    const int scale = (int)mlx_scale();   // 自适应：90640 是 10，90641 是 20
    int rows = mlx_rows();
    int cx = offset_x + disp_y * scale;             // 列坐标
    int cy = offset_y + (rows - 1 - disp_x) * scale; // 行坐标（屏幕上下翻转）
    tft.drawLine(cx - 5, cy, cx + 5, cy, TFT_WHITE);
    tft.drawLine(cx, cy - 5, cx, cy + 5, TFT_WHITE);
    tft.drawLine(cx - 2, cy, cx + 2, cy, TFT_BLACK);
    tft.drawLine(cx, cy - 2, cx, cy + 2, TFT_BLACK);

    // 在十字旁边标注最高温，读数更直观
    char buf[12];
    snprintf(buf, sizeof(buf), "%.1f", T_max_fp);
    int tx = cx + 8;
    int ty = cy - 14;
    if (tx > 320 - 36) tx = cx - 40;   // 靠右边缘时放到左边
    if (ty < 2) ty = cy + 6;           // 靠上边缘时放到下面
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(tx, ty);
    tft.print(buf);
}

// 底部信息条定义在 draw.hpp（同一次编译单元内，先声明后调用）
void draw_status_bar(unsigned long dt);

bool camera_decode_callback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (decode_target_buffer == nullptr) return 0;
    if (y >= decode_buffer_height) return 0;

    // 将解码的图像块复制到帧缓冲区
    for (int16_t row = 0; row < h; row++) {
        int16_t target_y = y + row;
        if (target_y >= decode_buffer_height) break;
        for (int16_t col = 0; col < w; col++) {
            int16_t target_x = x + col;
            if (target_x >= decode_buffer_width) break;
            decode_target_buffer[target_y * decode_buffer_width + target_x] = bitmap[row * w + col];
        }
    }
    return 1;
}

// 分配摄像头帧缓冲区
void init_fusion_buffers() {
    if (camera_frame_buffer == nullptr) {
        camera_frame_buffer = (uint16_t*)ps_malloc(320 * 240 * sizeof(uint16_t));
        if (camera_frame_buffer) {
            Serial.println("[Fusion] Camera frame buffer allocated in PSRAM");
        } else {
            Serial.println("[Fusion] Failed to allocate camera frame buffer!");
        }
    }
}

// 初始化全屏 Sprite（PSRAM），用于 alpha 融合
void init_fusion_sprite() {
    if (fusion_sprite == nullptr) {
        fusion_sprite = new TFT_eSprite(&tft);
        // 在 PSRAM 中创建 320x240 的 Sprite
        if (fusion_sprite->createSprite(320, 240)) {
            Serial.println("[Fusion] Fusion sprite created in PSRAM: 320x240");
        } else {
            Serial.println("[Fusion] Failed to create fusion sprite!");
            delete fusion_sprite;
            fusion_sprite = nullptr;
        }
    }
}

// 初始化画中画模式用的全屏 Sprite
void init_pip_sprite() {
    if (pip_sprite == nullptr) {
        pip_sprite = new TFT_eSprite(&tft);
        // 在 PSRAM 中创建 320x240 的 Sprite
        if (pip_sprite->createSprite(320, 240)) {
            Serial.println("[Fusion] PIP sprite created successfully: 320x240");
        } else {
            Serial.println("[Fusion] Failed to create PIP sprite!");
            delete pip_sprite;
            pip_sprite = nullptr;
        }
    }
}

// 释放摄像头帧缓冲区
void free_fusion_buffers() {
    if (camera_frame_buffer != nullptr) {
        free(camera_frame_buffer);
        camera_frame_buffer = nullptr;
        Serial.println("[Fusion] Camera frame buffer freed");
    }
    if (fusion_sprite != nullptr) {
        fusion_sprite->deleteSprite();
        delete fusion_sprite;
        fusion_sprite = nullptr;
        Serial.println("[Fusion] Fusion sprite deleted");
    }
    if (pip_sprite != nullptr) {
        pip_sprite->deleteSprite();
        delete pip_sprite;
        pip_sprite = nullptr;
        Serial.println("[Fusion] PIP sprite deleted");
    }
}

// 捕获并解码摄像头帧到缓冲区
bool capture_camera_frame() {
    if (!camera_ok) return false;
    if (camera_frame_buffer == nullptr) {
        init_fusion_buffers();
    }

    fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("[Fusion] Camera capture failed");
        return false;
    }

    if (fb->format == PIXFORMAT_JPEG) {
        // 设置目标缓冲区
        decode_target_buffer = camera_frame_buffer;
        decode_buffer_width = 320;
        decode_buffer_height = 240;

        TJpgDec.setJpgScale(1);
        TJpgDec.setSwapBytes(false);  // 与原始代码保持一致
        TJpgDec.setCallback(camera_decode_callback);

        // 清空头几行，确保解码从干净状态开始
        memset(camera_frame_buffer, 0, 320 * 240 * sizeof(uint16_t));

        // 解码 JPEG 到缓冲区
        TJpgDec.drawJpg(0, 0, fb->buf, fb->len);
        camera_frame_ready = true;
    }

    esp_camera_fb_return(fb);
    fb = NULL;
    return camera_frame_ready;
}

// RGB565 颜色字节序交换（大端 <-> 小端）
inline uint16_t swap_bytes(uint16_t color) {
    return (color >> 8) | (color << 8);
}

// Alpha 混合函数 - 混合两个 RGB565 颜色
// alpha: 0-255，0 表示完全使用背景色，255 表示完全使用前景色
inline uint16_t alpha_blend(uint16_t bg_color, uint16_t fg_color, uint8_t alpha) {
    if (alpha == 0) return bg_color;
    if (alpha == 255) return fg_color;

    // 1. 还原大小端 (因为 TFT_eSprite 存储的是字节交换后的 RGB565)
    bg_color = (bg_color << 8) | (bg_color >> 8);
    fg_color = (fg_color << 8) | (fg_color >> 8);

    // 提取 RGB565 的各分量
    uint8_t r1 = (bg_color >> 11) & 0x1F;
    uint8_t g1 = (bg_color >> 5) & 0x3F;
    uint8_t b1 = bg_color & 0x1F;

    uint8_t r2 = (fg_color >> 11) & 0x1F;
    uint8_t g2 = (fg_color >> 5) & 0x3F;
    uint8_t b2 = fg_color & 0x1F;

    // Alpha 混合
    uint8_t r = (r1 * (255 - alpha) + r2 * alpha) / 255;
    uint8_t g = (g1 * (255 - alpha) + g2 * alpha) / 255;
    uint8_t b = (b1 * (255 - alpha) + b2 * alpha) / 255;

    // 重组颜色
    uint16_t result = (r << 11) | (g << 5) | b;

    // 4. 再次交换字节序，以便存储到 TFT_eSprite 缓冲区
    return (result << 8) | (result >> 8);
}

// 从 RGB565 提取亮度值（用于判断是否为高温区域）
inline uint8_t get_luminance(uint16_t color) {
    uint8_t r = (color >> 11) & 0x1F;
    uint8_t g = (color >> 5) & 0x3F;
    uint8_t b = color & 0x1F;
    return (r + g + b) / 3;
}

// 渲染热成像到 Sprite（解决闪烁问题）
void render_thermal_to_sprite(TFT_eSprite* sprite, int width, int height) {
    Serial.println("[DEBUG] render_thermal_to_sprite start");
    // 安全检查
    if (sprite == nullptr) {
        Serial.println("[Fusion] Error: sprite is null");
        return;
    }
    Serial.println("[DEBUG] sprite ok");
    if (mlx90640To_buffer == nullptr) {
        Serial.println("[Fusion] Error: mlx90640To_buffer is null");
        return;
    }
    Serial.println("[DEBUG] mlx90640To_buffer ok");

    int cols = mlx_cols();
    int rows = mlx_rows();
    int scale = (int)mlx_scale();
    Serial.printf("[DEBUG] cols=%d, rows=%d, scale=%d\n", cols, rows, scale);

    // 限制渲染范围
    if (scale > 10) scale = 10;  // 防止过大的缩放
    Serial.printf("[DEBUG] after clamp scale=%d\n", scale);

    // 清空 Sprite
    Serial.println("[DEBUG] fillSprite start");
    sprite->fillSprite(TFT_BLACK);
    Serial.println("[DEBUG] fillSprite done");

    // 计算居中偏移
    int render_w = cols * scale;
    int render_h = rows * scale;
    int offset_x = (width - render_w) / 2;
    int offset_y = (height - render_h) / 2;
    Serial.printf("[DEBUG] render_w=%d, render_h=%d, offset_x=%d, offset_y=%d\n", render_w, render_h, offset_x, offset_y);

    // 边界检查
    if (offset_x < 0) offset_x = 0;
    if (offset_y < 0) offset_y = 0;

    // 渲染热成像到 Sprite（使用简单方法，更稳定）
    Serial.println("[DEBUG] render loop start");
    for (int i = 0; i < rows; i++) {
        int draw_y = i * scale + offset_y;
        if (draw_y >= height) break;

        for (int j = 0; j < cols; j++) {
            int draw_x = j * scale + offset_x;
            if (draw_x >= width) break;

            // 安全地访问缓冲区
            int buffer_idx = (rows - 1 - i) * cols + j;
            if (buffer_idx < 0 || buffer_idx >= cols * rows) {
                Serial.printf("[DEBUG] idx out of range: %d\n", buffer_idx);
                continue;
            }

            uint16_t color = colormap[mlx90640To_buffer[buffer_idx]];
            sprite->fillRect(draw_x, draw_y, scale, scale, color);
        }
    }
    Serial.println("[DEBUG] render loop done");

    // 注意：文本在主屏幕上绘制，避免 Sprite 字体问题
    Serial.println("[DEBUG] render_thermal_to_sprite end");
}

// ================= 摄像头取帧（RGB565） =================
// 本机摄像头是 OV7670，输出 RGB565 裸数据。
// 注意：实测有效帧是 320x216（138240 字节），不是 320x240（153600）——
// OV7670 的窗口比 QVGA 少若干行。这里按 fb->len 反推实际高度，避免因为
// 长度检查不过而整帧被丢弃。
static int camera_frame_h = 216;   // 最近一次取到的有效行数

// 全局：屏幕上最近一次实际显示的画面缓冲，供 shot 命令发给 PC 检查
uint16_t* g_camera_frame = nullptr;  // 纯摄像头模式
uint16_t* g_last_frame = nullptr;    // 融合模式

static bool camera_grab_rgb565(uint16_t* dst, bool swap_bytes = false) {
    if (dst == nullptr) return false;
    fb = esp_camera_fb_get();
    if (!fb) return false;
    if (fb->format != PIXFORMAT_RGB565) {
        esp_camera_fb_return(fb);
        fb = NULL;
        return false;
    }
    int h = fb->len / (320 * 2);     // 实际行数（实测 240）
    if (h <= 0) {
        esp_camera_fb_return(fb);
        fb = NULL;
        return false;
    }
    if (h > 240) h = 240;
    camera_frame_h = h;

    const uint16_t* src = (const uint16_t*)fb->buf;
    static bool px_logged = false;
    if (!px_logged) {
        Serial.printf("[Cam] len=%u w=%d h=%d first pixels:", (unsigned)fb->len, (int)fb->width, (int)fb->height);
        for (int i = 0; i < 8; i++) Serial.printf(" %04X", src[i]);
        Serial.println();
        // 中间一行也取几个，避开可能的行首噪声
        const uint16_t* mid = src + (size_t)(h / 2) * 320 + 150;
        Serial.printf("[Cam] mid pixels:");
        for (int i = 0; i < 8; i++) Serial.printf(" %04X", mid[i]);
        Serial.println();
        px_logged = true;
    }
    // OV7670 的 set_vflip()/set_hmirror() 都不生效，翻转全部在软件层做。
    // camera_vflip: 上下翻转；camera_hmirror: 左右镜像。
    // 性能优化：一次处理 32-bit（两个像素），字节交换用掩码+移位，
    // 比逐像素循环快 2~3 倍；镜像时 32-bit 反向读依然连续。
    for (int y = 0; y < h; y++) {
        const uint16_t* srow = src + (size_t)(camera_vflip ? (h - 1 - y) : y) * 320;
        uint16_t* drow = dst + (size_t)y * 320;
        if (swap_bytes) {
            uint32_t* d32 = (uint32_t*)drow;
            const uint32_t* s32 = (const uint32_t*)srow;
            if (camera_hmirror) {
                for (int i = 0; i < 160; i++) {
                    uint32_t v = s32[159 - i];
                    d32[i] = ((v & 0x00FF00FFu) << 8) | ((v & 0xFF00FF00u) >> 8);
                }
            } else {
                for (int i = 0; i < 160; i++) {
                    uint32_t v = s32[i];
                    d32[i] = ((v & 0x00FF00FFu) << 8) | ((v & 0xFF00FF00u) >> 8);
                }
            }
        } else if (camera_hmirror) {
            uint32_t* d32 = (uint32_t*)drow;
            const uint32_t* s32 = (const uint32_t*)srow;
            for (int i = 0; i < 160; i++) {
                d32[i] = s32[159 - i];
            }
        } else {
            memcpy(drow, srow, 320 * sizeof(uint16_t));
        }
    }
    if (h < 240) {
        memset(dst + (size_t)320 * h, 0, (size_t)320 * (240 - h) * sizeof(uint16_t));
    }

    esp_camera_fb_return(fb);
    fb = NULL;
    return true;
}

// ================= 模式 1: 仅摄像头 =================
void draw_camera_only() {
    if (!camera_ok) {
        tft.fillScreen(TFT_BLACK);
        tft.setCursor(80, 110);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setTextSize(2);
        tft.println("Camera Error");
        return;
    }

    if (g_camera_frame == nullptr) {
        g_camera_frame = (uint16_t*)ps_malloc(320 * 240 * sizeof(uint16_t));
        if (g_camera_frame == nullptr) {
            Serial.println("[Camera] full frame buffer alloc failed");
            return;
        }
    }
    if (!camera_grab_rgb565(g_camera_frame, false)) return;

    tft.startWrite();
    tft.pushImage(0, 0, 320, camera_frame_h, g_camera_frame);
    tft.endWrite();

    draw_status_bar(0);   // 纯摄像头模式也显示状态栏
}

// ================= 模式 2: 画中画 (摄像头主画面 + 热成像小窗) =================
// 使用 TFT_eSprite 作为全页缓冲，替代直接数组操作
void draw_pip_thermal() {
    // 安全检查
    if (mlx90640To_buffer == nullptr || !flag_sensor_ok) {
        draw_camera_only();
        return;
    }

    if (!camera_ok) {
        extern void draw();
        draw();
        return;
    }

    // 初始化 PIP Sprite（用于全页缓冲）
    if (pip_sprite == nullptr) {
        init_pip_sprite();
    }
    if (pip_sprite == nullptr) {
        // Sprite 分配失败，回退到直接渲染
        draw_camera_only();
        return;
    }

    // 清空 Sprite 为黑色背景
    pip_sprite->fillSprite(TFT_BLACK);

    // 1. 获取摄像头帧并直接解码到 Sprite 内存
    fb = esp_camera_fb_get();
    if (!fb || fb->format != PIXFORMAT_JPEG) {
        if (fb) {
            esp_camera_fb_return(fb);
            fb = NULL;
        }
        extern void draw();
        draw();
        return;
    }

    // 直接解码到 Sprite 的缓冲区，省去中间拷贝
    uint16_t* sprite_buffer = (uint16_t*)pip_sprite->getPointer();
    if (sprite_buffer == nullptr) {
        esp_camera_fb_return(fb);
        fb = NULL;
        draw_camera_only();
        return;
    }

    decode_target_buffer = sprite_buffer;
    decode_buffer_width = 320;
    decode_buffer_height = 240;

    TJpgDec.setJpgScale(1);
    TJpgDec.setSwapBytes(true);  // TFT_eSprite 使用交换后的字节序
    TJpgDec.setCallback(camera_decode_callback);
    TJpgDec.drawJpg(0, 0, fb->buf, fb->len);

    esp_camera_fb_return(fb);
    fb = NULL;

    // 2. 在 Sprite 中绘制热成像小窗
    int cols = mlx_cols();
    int rows = mlx_rows();
    // 动态计算 scale，让热成像尽可能填满小窗
    int scale_x = PIP_WIDTH / cols;   // 128 / 32 = 4
    int scale_y = PIP_HEIGHT / rows;  // 96 / 24 = 4
    int scale = (scale_x < scale_y) ? scale_x : scale_y;  // 取较小值，确保不超出
    if (scale < 1) scale = 1;

    // 计算热成像偏移（在小窗中居中）
    int thermal_w = cols * scale;
    int thermal_h = rows * scale;
    int inner_offset_x = (PIP_WIDTH - thermal_w) / 2;
    int inner_offset_y = (PIP_HEIGHT - thermal_h) / 2;

    // 在小窗区域绘制黑色背景和白色边框
    pip_sprite->fillRect(PIP_POS_X, PIP_POS_Y, PIP_WIDTH, PIP_HEIGHT, TFT_BLACK);
    pip_sprite->drawRect(PIP_POS_X - 2, PIP_POS_Y - 2, PIP_WIDTH + 4, PIP_HEIGHT + 4, TFT_WHITE);

    // 渲染热成像到小窗区域
    for (int i = 0; i < rows; i++) {
        int draw_y = PIP_POS_Y + inner_offset_y + i * scale;
        if (draw_y >= 240) break;

        for (int j = 0; j < cols; j++) {
            int draw_x = PIP_POS_X + inner_offset_x + j * scale;
            if (draw_x >= 320) break;

            int buffer_idx = (rows - 1 - i) * cols + j;
            if (buffer_idx < 0 || buffer_idx >= cols * rows) continue;

            uint16_t color = colormap[mlx90640To_buffer[buffer_idx]];
            pip_sprite->fillRect(draw_x, draw_y, scale, scale, color);
        }
    }

    // 3. 一次性推送整个 Sprite 到屏幕（无闪烁！）
    pip_sprite->pushSprite(0, 0);
}

// ================= 模式 3: 画中画 (热成像主画面 + 摄像头小窗) =================
// 使用 TFT_eSprite 作为全页缓冲
void draw_pip_camera() {
    // 安全检查：确保 MLX 数据已准备好
    if (mlx90640To_buffer == nullptr || !flag_sensor_ok) {
        return;
    }

    // 初始化 PIP Sprite
    if (pip_sprite == nullptr) {
        init_pip_sprite();
    }
    if (pip_sprite == nullptr) {
        return;
    }

    // 清空 Sprite 为黑色背景
    pip_sprite->fillSprite(TFT_BLACK);

    // 1. 渲染热成像主画面到 Sprite
    int cols = mlx_cols();
    int rows = mlx_rows();
    int scale = (int)mlx_scale();

    // 渲染热成像（居中）
    int thermal_w = cols * scale;
    int thermal_h = rows * scale;
    int offset_x = (320 - thermal_w) / 2;
    int offset_y = (240 - thermal_h) / 2;

    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            int buffer_idx = (rows - 1 - i) * cols + j;
            if (buffer_idx < 0 || buffer_idx >= cols * rows) continue;
            uint16_t color = colormap[mlx90640To_buffer[buffer_idx]];

            int start_x = offset_x + j * scale;
            int start_y = offset_y + i * scale;

            pip_sprite->fillRect(start_x, start_y, scale, scale, color);
        }
    }

    // 2. 如果有摄像头，在小窗位置绘制摄像头画面
    if (camera_ok && capture_camera_frame()) {
        // 缩放摄像头图像到小窗
        static uint16_t camera_small[PIP_WIDTH * PIP_HEIGHT];
        const int batch_size = 16;

        for (int y_batch = 0; y_batch < PIP_HEIGHT; y_batch += batch_size) {
            yield();
            delay(1);

            for (int y = y_batch; y < y_batch + batch_size && y < PIP_HEIGHT; y++) {
                for (int x = 0; x < PIP_WIDTH; x++) {
                    int src_x = x * 320 / PIP_WIDTH;
                    int src_y = y * 240 / PIP_HEIGHT;
                    if (src_x >= 320) src_x = 319;
                    if (src_y >= 240) src_y = 239;
                    camera_small[y * PIP_WIDTH + x] = camera_frame_buffer[src_y * 320 + src_x];
                }
            }
        }

        // 将小窗摄像头图像绘制到 Sprite（带白色边框）
        pip_sprite->drawRect(PIP_POS_X - 2, PIP_POS_Y - 2, PIP_WIDTH + 4, PIP_HEIGHT + 4, TFT_WHITE);

        // 小窗图像 - 使用 pushImage 更高效，并自动处理字节序
        // 注意：camera_small 中的数据需要字节序转换
        static uint16_t camera_swapped[PIP_WIDTH * PIP_HEIGHT];
        for (int i = 0; i < PIP_WIDTH * PIP_HEIGHT; i++) {
            uint16_t color = camera_small[i];
            camera_swapped[i] = (color << 8) | (color >> 8);
        }
        pip_sprite->pushImage(PIP_POS_X, PIP_POS_Y, PIP_WIDTH, PIP_HEIGHT, camera_swapped);
    }

    // 3. 一次性推送整个 Sprite 到屏幕（无闪烁！）
    pip_sprite->pushSprite(0, 0);
}


// ================= 模式 4: Alpha融合 (热成像叠加在摄像头上) =================
// 热成像与摄像头的整数 alpha 混合（在正常 RGB565 字节序上计算）
static inline uint16_t blend_rgb565(uint16_t cam, uint16_t therm, uint8_t alpha) {
    // alpha 为热成像占比（0-255）
    // 性能：用 >>8 代替 /255，除法在 ESP32 上很慢，这里误差 <0.5% 肉眼无感
    uint16_t cr = (cam >> 11) & 0x1F, cg = (cam >> 5) & 0x3F, cb = cam & 0x1F;
    uint16_t tr = (therm >> 11) & 0x1F, tg = (therm >> 5) & 0x3F, tb = therm & 0x1F;
    uint16_t inv = 255 - alpha;
    uint16_t r = (uint16_t)((cr * inv + tr * alpha) >> 8);
    uint16_t g = (uint16_t)((cg * inv + tg * alpha) >> 8);
    uint16_t b = (uint16_t)((cb * inv + tb * alpha) >> 8);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

// 全局：最近一帧实际画面（融合模式下是混合结果，供 shot 命令发到 PC 检查）

// 模式 4：热成像半透明覆盖在摄像头上（固定 50%，不做配准变换）
// 实现要点：
//   1. 整帧混合到 PSRAM 缓冲，最后只调用一次 pushImage —— 避免逐行推屏与主循环争抢 SPI
//   2. 全部整数运算，无浮点
//   3. 不依赖 TFT_eSprite（精灵的字节序路径和普通推图不一致，之前花屏就是它）
void draw_thermal_overlay() {
    if (!flag_sensor_ok || mlx90640To_buffer == nullptr) {
        draw_camera_only();
        return;
    }
    if (!camera_ok) {
        extern void draw();
        draw();
        return;
    }

    static uint16_t* cam_buf = nullptr;
    static uint16_t* out_buf = nullptr;
    if (cam_buf == nullptr) cam_buf = (uint16_t*)ps_malloc(320 * 240 * sizeof(uint16_t));
    if (out_buf == nullptr) {
        // 输出缓冲优先放内部 RAM：逐像素混合时 PSRAM 的访问延迟是主要瓶颈，
        // 放内部 RAM 能显著减少每像素的读写等待。分配失败再退回 PSRAM。
        out_buf = (uint16_t*)heap_caps_malloc(320 * 240 * sizeof(uint16_t),
                                              MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (out_buf == nullptr) {
            out_buf = (uint16_t*)ps_malloc(320 * 240 * sizeof(uint16_t));
            Serial.println("[Fusion] out_buf fell back to PSRAM");
        } else {
            Serial.println("[Fusion] out_buf allocated in internal RAM");
        }
    }
    if (cam_buf == nullptr || out_buf == nullptr) {
        draw_camera_only();
        return;
    }

    // 关键：摄像头帧在内存里是大端字节序，blend 必须按逻辑值计算，
    // 所以这里先交换成正常数值（swap=true）。
    unsigned long t_grab0 = millis();
    if (!camera_grab_rgb565(cam_buf, true)) {
        extern void draw();
        draw();
        return;
    }
    unsigned long t_grab1 = millis();

    int cols = mlx_cols();
    int rows = mlx_rows();
    int scale = (int)mlx_scale();
    if (scale < 1) scale = 1;
    const uint8_t alpha = 128;   // 固定 50% 半透明

    unsigned long t0 = millis();

    // 用双线性插值把 16x12 的温度平滑放大，避免出现马赛克方块
    init_interp_tables(cols, rows, scale);

    // 性能优化：垂直方向两行算一行再复制。
    // 热成像本身是 16x12 插值放大的，垂直分辨率减半肉眼几乎看不出，
    // 但能把最耗时的"插值+混合"循环砍掉一半。
    for (int y = 0; y < 240; y += 2) {
        const uint16_t* cam_row = cam_buf + (size_t)y * 320;
        uint16_t* dst_row = out_buf + (size_t)y * 320;

        for (int x = 0; x < 320; x++) {
            int value = bio_linear_interpolation(x, 240 - 1 - y, mlx90640To_buffer, cols, rows);
            if (value < 0) value = 0;
            if (value > 179) value = 179;
            uint16_t mixed = blend_rgb565(cam_row[x], colormap[value], alpha);
            // 写回缓冲区前交换回大端存储，pushImage 才能正确显示
            dst_row[x] = (uint16_t)((mixed >> 8) | (mixed << 8));
        }
        if (y + 1 < 240) {
            memcpy(out_buf + (size_t)(y + 1) * 320, dst_row, 320 * sizeof(uint16_t));
        }
    }

    // 一次推屏
    unsigned long t_blend1 = millis();

    tft.startWrite();
    tft.pushImage(0, 0, 320, 240, out_buf);
    tft.endWrite();

    {
        unsigned long t_push1 = millis();
        static int perf_cnt = 0;
        if (++perf_cnt % 20 == 0) {
            Serial.printf("[Perf] grab=%lu blend=%lu push=%lu total=%lu\n",
                          t_grab1 - t_grab0, t_blend1 - t_grab1,
                          t_push1 - t_blend1, t_push1 - t_grab0);
        }
    }

    g_last_frame = out_buf;

    draw_status_bar(millis() - t0);
    draw_trace_cross_fusion(0, 0);   // 十字画在状态栏之后，保证不被遮挡
}

// ================= 主渲染函数 =================
void draw_fusion();

// 获取当前模式名称 (提前声明，供 save_display_mode_to_eeprom 使用)
const char* get_display_mode_name() {
    switch (current_display_mode) {
        case MODE_THERMAL_ONLY:    return "Thermal Only";
        case MODE_CAMERA_ONLY:     return "Camera Only";
        case MODE_PIP_THERMAL:     return "PiP Thermal";
        case MODE_PIP_CAMERA:      return "PiP Camera";
        case MODE_THERMAL_OVERLAY: return "Thermal Overlay";
        default:                   return "Unknown";
    }
}

// 保存显示模式到 EEPROM
void save_display_mode_to_eeprom() {
    uint8_t mode = (uint8_t)current_display_mode;
    EEPROM.write(EEPROM_ADDR_DISPLAY_MODE, mode);
    EEPROM.commit();
    Serial.printf("[Fusion] Display mode saved to EEPROM: %d (%s)\n",
                  current_display_mode, get_display_mode_name());
}

// 从 EEPROM 读取显示模式
DisplayMode load_display_mode_from_eeprom() {
    uint8_t mode = EEPROM.read(EEPROM_ADDR_DISPLAY_MODE);
    // 验证模式值是否有效
    if (mode >= 0 && mode <= 4) {
        return (DisplayMode)mode;
    }
    // 无效值返回默认值
    return DEFAULT_DISPLAY_MODE;
}

// ================= 模式切换函数 =================
void set_display_mode(DisplayMode mode) {
    // 纯热成像模式显示原始画面（不做双线性插值），其余模式保持插值
    use_upsample = (mode != MODE_THERMAL_ONLY);
    if (mode != current_display_mode) {
        current_display_mode = mode;
        Serial.printf("[Fusion] Display mode changed to: %d\n", mode);

        // 保存到 EEPROM
        save_display_mode_to_eeprom();

        // 根据模式分配/释放资源
        if (mode == MODE_PIP_CAMERA) {
            init_fusion_buffers();
        }

        // Alpha 融合模式需要全屏 Sprite
        if (mode == MODE_THERMAL_OVERLAY) {
            init_fusion_sprite();
        }

        // 如果切换到纯热成像，可以释放资源
        if (mode == MODE_THERMAL_ONLY) {
            // free_fusion_buffers();  // 可选
        }
    }
}

// 切换到下一个显示模式（只保留三种：融合 -> 纯摄像头 -> 纯热成像 -> 融合）
void next_display_mode() {
    switch (current_display_mode) {
        case MODE_THERMAL_OVERLAY: set_display_mode(MODE_CAMERA_ONLY);     break;
        case MODE_CAMERA_ONLY:     set_display_mode(MODE_THERMAL_ONLY);    break;
        default:                   set_display_mode(MODE_THERMAL_OVERLAY); break;
    }
}

// ================= 主渲染函数实现 =================
void draw_fusion() {
    switch (current_display_mode) {
        case MODE_THERMAL_ONLY:
            extern void draw();
            draw();
            break;

        case MODE_CAMERA_ONLY:
            draw_camera_only();
            break;

        case MODE_PIP_THERMAL:
            draw_pip_thermal();
            break;

        case MODE_PIP_CAMERA:
            draw_pip_camera();
            break;

        case MODE_THERMAL_OVERLAY:
            draw_thermal_overlay();
            break;

        default:
            extern void draw();
            draw();
            break;
    }
}
