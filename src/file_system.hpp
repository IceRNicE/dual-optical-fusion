#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>
#include "shared_val.hpp"

// ================= 持久化存储 =================
const char* ALIGN_CONFIG_FILE = "/align.cfg";

// 对齐参数
extern float align_tx;
extern float align_ty;
extern float align_sx;
extern float align_sy;
extern float align_ang;
extern uint8_t fusion_alpha;
extern bool camera_vflip;
extern bool camera_hmirror;

// 文件系统初始化
void fs_init() {
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed!");
        return;
    }
    Serial.println("LittleFS mounted successfully");
}

// 文件系统命令处理
void fs_cli(String cmd) {
    cmd.trim();

    if (cmd.startsWith("ls")) {
        // 列出目录
        String path = "/";
        if (cmd.length() > 2) {
            path = cmd.substring(3);
            if (!path.startsWith("/")) {
                path = "/" + path;
            }
        }

        File dir = LittleFS.open(path);
        if (!dir) {
            Serial.println("Failed to open directory");
            return;
        }

        File file = dir.openNextFile();
        while (file) {
            Serial.printf("%-15s %8d bytes\n", file.name(), file.size());
            file = dir.openNextFile();
        }
    } else if (cmd.startsWith("rm")) {
        // 删除文件
        String path = cmd.substring(3);
        path.trim();
        if (path.isEmpty()) {
            Serial.println("Usage: rm <filename>");
            return;
        }

        if (!path.startsWith("/")) {
            path = "/" + path;
        }

        if (LittleFS.remove(path)) {
            Serial.println("File removed");
        } else {
            Serial.println("File not found or remove failed");
        }
    } else if (cmd.startsWith("cat")) {
        // 读取文件内容
        String path = cmd.substring(4);
        path.trim();
        if (path.isEmpty()) {
            Serial.println("Usage: cat <filename>");
            return;
        }

        if (!path.startsWith("/")) {
            path = "/" + path;
        }

        File file = LittleFS.open(path, "r");
        if (!file) {
            Serial.println("File not found");
            return;
        }

        while (file.available()) {
            Serial.write(file.read());
        }
        Serial.println();
        file.close();
    } else if (cmd.startsWith("df")) {
        // 显示磁盘使用情况
        size_t total = LittleFS.totalBytes();
        size_t used = LittleFS.usedBytes();
        size_t free = total - used;

        Serial.printf("Total: %d bytes\n", total);
        Serial.printf("Used: %d bytes\n", used);
        Serial.printf("Free: %d bytes\n", free);
        Serial.printf("Usage: %.2f%%\n", (float)used / (float)total * 100.0f);
    }
}

// 保存对齐参数到文件
void save_align_params() {
    File file = LittleFS.open(ALIGN_CONFIG_FILE, "w");
    if (!file) {
        Serial.println("Failed to open config file for writing");
        return;
    }

    // 写入仿射变换参数
    file.write((uint8_t*)&align_tx, sizeof(float));
    file.write((uint8_t*)&align_ty, sizeof(float));
    file.write((uint8_t*)&align_sx, sizeof(float));
    file.write((uint8_t*)&align_sy, sizeof(float));
    file.write((uint8_t*)&align_ang, sizeof(float));

    // 写入透明度
    file.write((uint8_t*)&fusion_alpha, sizeof(uint8_t));
    // 写入翻转状态
    file.write((uint8_t)camera_vflip);
    file.write((uint8_t)camera_hmirror);

    file.close();
    Serial.println("Config (Align + Alpha + Flip) saved");
}

// 从文件加载对齐参数
void load_align_params() {
    if (!LittleFS.exists(ALIGN_CONFIG_FILE)) {
        Serial.println("Config file not found, using defaults.");
        return;
    }

    File file = LittleFS.open(ALIGN_CONFIG_FILE, "r");
    if (!file) {
        Serial.println("Failed to open config file");
        return;
    }

    // 基础检查：至少要有5个浮点数
    if (file.size() < 5 * sizeof(float)) {
        Serial.println("Config file corrupted");
        file.close();
        return;
    }

    // 读取仿射变换参数
    file.read((uint8_t*)&align_tx, sizeof(float));
    file.read((uint8_t*)&align_ty, sizeof(float));
    file.read((uint8_t*)&align_sx, sizeof(float));
    file.read((uint8_t*)&align_sy, sizeof(float));
    file.read((uint8_t*)&align_ang, sizeof(float));

    // 尝试读取透明度 (兼容旧配置文件)
    if (file.available() > 0) {
        file.read((uint8_t*)&fusion_alpha, sizeof(uint8_t));
    }

    if (file.available() >= 2) {
        camera_vflip = (bool)file.read();
        camera_hmirror = (bool)file.read();
    }

    // 本机是 OV7670，画面天生上下颠倒 + 左右镜像，且它的
    // set_vflip()/set_hmirror() 都不生效，只能在软件层翻转。
    // 旧固件写下的 align.cfg 里 flip 字节不可靠，这里强制覆盖。
    camera_vflip = true;
    camera_hmirror = true;

    file.close();
    Serial.printf("Config loaded: Align(%.2f,%.2f,%.2f,%.2f,%.2f) Alpha(%d)\n",
                  align_tx, align_ty, align_sx, align_sy, align_ang, fusion_alpha);
}

// 设置对齐参数
void handle_set_align(String cmd) {
    float params[5];
    int start = 0;
    int index = 0;

    cmd = cmd.substring(10); // 移除 "set_align "
    cmd.trim();

    while (index < 5) {
        int spaceIndex = cmd.indexOf(' ', start);
        if (spaceIndex == -1) spaceIndex = cmd.length();
        String p = cmd.substring(start, spaceIndex);
        params[index++] = p.toFloat();
        start = spaceIndex + 1;
        if (start >= cmd.length()) break;
    }

    if (index >= 5) {
        align_tx = params[0];
        align_ty = params[1];
        align_sx = params[2];
        align_sy = params[3];
        align_ang = params[4];
        save_align_params(); // 保存
        Serial.println("OK: Align params updated");
    } else {
        Serial.println("Err: Invalid align params");
    }
}

// 设置融合透明度
void handle_set_alpha(String cmd) {
    cmd = cmd.substring(10); // 移除 "set_alpha "
    cmd.trim();
    int val = cmd.toInt();

    if (val < 0) val = 0;
    if (val > 255) val = 255;

    fusion_alpha = (uint8_t)val;
    save_align_params(); // 保存
    Serial.printf("OK: Alpha set to %d\n", fusion_alpha);
}