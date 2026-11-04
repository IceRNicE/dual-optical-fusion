#pragma once
#include <Arduino.h>
#include "screen.hpp"
#include "fusion.hpp"
#include "file_system.hpp"
#include "camera.hpp"

// 串口波特率：调试抓帧要传一帧 153600 字节。实测 2Mbps 时 CH340 会偶发丢字节，
// 921600 稳定且约 1.7 秒/帧，够调试用。platformio.ini 的 monitor_speed 需同步。
#ifndef SERIAL_BAUD
#define SERIAL_BAUD 115200
#endif

void serial_start() {
  Serial.begin(SERIAL_BAUD);
  while (!Serial) {
    ; // 等待串口连接
  }
  Serial.println("Serial communication initialized.");
  // 初始化文件系统
  fs_init();
  // 加载对齐参数
  load_align_params();
}

// 应用摄像头翻转
void camera_apply_flip() {
    if (!camera_ok) return;
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_vflip(s, camera_vflip);
        s->set_hmirror(s, camera_hmirror);
    }
}

// 设置摄像头翻转
void camera_set_flip(bool vflip, bool hmirror) {
    camera_vflip = vflip;
    camera_hmirror = hmirror;
    camera_apply_flip();
    save_align_params();
}

// Supports 'h' for help menu
// Supports 'echo' for echoing input
// Supports 'screen' commands for display control
// Supports 'mode' commands for fusion mode switching
// Supports 'set_align', 'get_align', 'set_alpha' commands for affine transform
// Supports 'ls', 'rm', 'df' commands for file system operations
void serial_loop(){
    if (Serial.available() > 0) {
        String input = Serial.readStringUntil('\n');
        input.trim(); // Remove leading and trailing whitespaces

        if (input == "h") {
            // ==== English Help Menu ====
            Serial.println("\r\n=========================================");
            Serial.println("          ESP32 Serial Console           ");
            Serial.println("=========================================");
            Serial.println("[ General Commands ]");
            Serial.println("  h                     - Show this help message");
            Serial.println("  echo <message>        - Echo the input message back to you");
            Serial.println("");
            Serial.println("[ Screen Control ]");
            Serial.println("  screen on             - Turn on the screen smoothly");
            Serial.println("  screen off            - Turn off the screen smoothly");
            Serial.println("  screen brightness <X> - Set brightness level (X: 5~255)");
            Serial.println("");
            Serial.println("[ Display Mode (Fusion) - Auto-saved to EEPROM ]");
            Serial.println("  mode                  - Show current display mode");
            Serial.println("  mode next             - Switch to next display mode");
            Serial.println("  mode <0-4>            - Set specific mode:");
            Serial.println("                          0=Thermal Only");
            Serial.println("                          1=Camera Only");
            Serial.println("                          2=PiP (Camera main + Thermal sub) [DEFAULT]");
            Serial.println("                          3=PiP (Thermal main + Camera sub)");
            Serial.println("                          4=Thermal Overlay on Camera");
            Serial.println("");
            Serial.println("[ File System Commands ]");
            Serial.println("  ls                    - List files in root directory");
            Serial.println("  rm <filename>         - Remove a file");
            Serial.println("  df                    - Show disk usage information");
            Serial.println("");
            Serial.println("[ Fusion Configuration ]");
            Serial.println("  set_align tx ty sx sy ang - Set alignment params");
            Serial.println("  get_align             - Get current alignment params");
            Serial.println("  set_alpha val         - Set fusion transparency (0-255)");
            Serial.println("  toggle_vflip          - Toggle vertical flip");
            Serial.println("  toggle_hflip          - Toggle horizontal flip");
            Serial.println("Note: Mode and align params are auto-saved and restored on boot.");
            Serial.println("=========================================\r\n");

        } else if (input.startsWith("echo ")) {
            String message = input.substring(5); // Extract message after "echo "
            Serial.println("Echo: " + message);

        } else if (input.startsWith("screen ") || input.startsWith("color_reverse ")) {  // Intercept screen control commands
            screen_cli(input);

        } else if (input == "mode") {
            Serial.printf("[Mode] Current: %s (%d)\n", get_display_mode_name(), current_display_mode);

        } else if (input == "mode next") {
            next_display_mode();
            Serial.printf("[Mode] Switched to: %s (%d)\n", get_display_mode_name(), current_display_mode);

        } else if (input.startsWith("mode ")) {
            String mode_str = input.substring(5);
            int mode = mode_str.toInt();
            if (mode >= 0 && mode <= 4) {
                set_display_mode((DisplayMode)mode);
                Serial.printf("[Mode] Set to: %s (%d)\n", get_display_mode_name(), current_display_mode);
            } else {
                Serial.println("[Error] Invalid mode. Use 0-4 or 'next'.");
            }

        } else if (input.startsWith("ls") || input.startsWith("rm") || input.startsWith("df")) {
            fs_cli(input);

        } else if (input.startsWith("set_align")) {
            handle_set_align(input);

        } else if (input.startsWith("get_align")) {
            Serial.printf("ALIGN %.2f %.2f %.3f %.3f %.2f %d\n",
                align_tx, align_ty, align_sx, align_sy, align_ang, fusion_alpha);

        } else if (input.startsWith("set_alpha")) {
            handle_set_alpha(input);

        } else if (input.startsWith("toggle_vflip")) {
            camera_vflip = !camera_vflip;
            camera_apply_flip();
            save_align_params();
            Serial.printf("VFLIP:%d\n", camera_vflip);

        } else if (input.startsWith("toggle_hflip")) {
            camera_hmirror = !camera_hmirror;
            camera_apply_flip();
            save_align_params();
            Serial.printf("HFLIP:%d\n", camera_hmirror);

        } else if (input.startsWith("set_flip")) {
            int v = 0, h = 0;
            sscanf(input.c_str(), "set_flip %d %d", &v, &h);
            camera_set_flip(v == 1, h == 1);
            Serial.printf("OK: Flip set to V:%d H:%d\n", v, h);

        }else if (input.startsWith("top")) {
            print_heap_usage();

        } else if (input == "shot") {
            // 把最近一帧实际画面（融合结果优先，否则摄像头）降到 160x120 发到 PC。
            // 配合 tools/shot.py 使用：先发一行文本头，再发原始 RGB565 字节。
            extern uint16_t* g_last_frame;
            extern bool camera_grab_rgb565(uint16_t* dst, bool swap_bytes);

            static uint16_t* shot_buf = nullptr;
            if (shot_buf == nullptr) shot_buf = (uint16_t*)ps_malloc(160 * 120 * sizeof(uint16_t));

            // 直接发屏幕上正在显示的那一帧，避免重新取帧时撞上撕裂帧（EV-EOF-OVF）
            extern uint16_t* g_camera_frame;
            const uint16_t* src = g_last_frame ? g_last_frame : g_camera_frame;

            if (src && shot_buf) {
                for (int y = 0; y < 120; y++) {
                    for (int x = 0; x < 160; x++) {
                        shot_buf[y * 160 + x] = src[(y * 2) * 320 + (x * 2)];
                    }
                }
                Serial.printf("SHOT 160 120 %u\n", (unsigned)(160 * 120 * 2));
                Serial.flush();
                Serial.write((const uint8_t*)shot_buf, 160 * 120 * 2);
                Serial.flush();
            } else {
                Serial.println("SHOT FAIL");
            }

        } else if (input.startsWith("cal")) {
            cal_cli(input);

        } else if (input == "bat") {
            extern float bat_v;
            Serial.printf("BAT %.2fV %d%%\n", bat_v, vbat_percent);

        } else if (input == "maxpos") {
            Serial.printf("MAXPOS %d %d T=%.2f\n", x_max, y_max, T_max_fp);

        } else if (input == "testcolor") {
            // 颜色校准测试：上排用 fillRect（不走换字节），下排用 pushImage（走换字节）。
            // 上排显示 红/绿/蓝 三块；下排用同一组 RGB565 值推图。
            // 两排颜色一致 → 不需要预交换；下排红变蓝 → pushImage 会交换，数据需预交换。
            Serial.println("[Test] color bars: top=fillRect red/green/blue, bottom=pushImage red/green/blue");
            tft.startWrite();
            tft.fillRect(0, 0, 106, 118, TFT_RED);
            tft.fillRect(107, 0, 106, 118, TFT_GREEN);
            tft.fillRect(214, 0, 106, 118, TFT_BLUE);

            uint16_t* bar_buf = (uint16_t*)ps_malloc(320 * 118 * sizeof(uint16_t));
            if (bar_buf) {
                for (int i = 0; i < 320 * 118; i++) {
                    if (i < 320 * 39) bar_buf[i] = 0xF800;        // 红
                    else if (i < 320 * 78) bar_buf[i] = 0x07E0;   // 绿
                    else bar_buf[i] = 0x001F;                     // 蓝
                }
                tft.pushImage(0, 120, 320, 118, bar_buf);
                free(bar_buf);
            }
            tft.endWrite();
            Serial.println("[Test] done");

        } else if (input.length() > 0) { // Prevent blank enter keys from triggering unknown command
            Serial.println("Unknown command: '" + input + "'. Type 'h' for help.");
        }
    }
}