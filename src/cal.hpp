#pragma once
// 温度校准模块
//
// 协议（与配套上位机 unified_tool 匹配，格式来自上位机反汇编）：
//   cal list                          -> 逐行输出 "ID: n, Range: [lo, hi]C, Gain: g, Offset: o"
//   cal add <lower> <upper> <offset> <gain>
//   cal del <id>
//
// 校准模型：落在 [lower, upper] 区间内的温度做线性修正
//   T' = T * gain + offset
// 区间可以有多段（最多 MAX_CAL_POINTS 段），彼此不重叠。

#include <Arduino.h>
#include <LittleFS.h>
#include "shared_val.hpp"

#define MAX_CAL_POINTS 16
#define CAL_FILE "/cal.bin"

struct CalPoint {
    float lower;
    float upper;
    float offset;
    float gain;
};

static CalPoint cal_points[MAX_CAL_POINTS];
static int cal_count = 0;

static void cal_defaults() {
    cal_count = 0;
}

static bool cal_load() {
    if (!LittleFS.begin(true)) {
        cal_defaults();
        return false;
    }
    File f = LittleFS.open(CAL_FILE, "r");
    if (!f) {
        cal_defaults();
        return false;
    }
    int n = f.read();
    if (n < 0 || n > MAX_CAL_POINTS) {
        f.close();
        cal_defaults();
        return false;
    }
    cal_count = n;
    for (int i = 0; i < n; i++) {
        f.read((uint8_t*)&cal_points[i], sizeof(CalPoint));
    }
    f.close();
    return true;
}

static bool cal_save() {
    File f = LittleFS.open(CAL_FILE, "w");
    if (!f) return false;
    f.write((uint8_t)cal_count);
    f.write((const uint8_t*)cal_points, sizeof(CalPoint) * cal_count);
    f.close();
    return true;
}

// 对单点温度应用校准（多区间时取第一个命中的）
inline float cal_apply_point(float t) {
    for (int i = 0; i < cal_count; i++) {
        if (t >= cal_points[i].lower && t <= cal_points[i].upper) {
            return t * cal_points[i].gain + cal_points[i].offset;
        }
    }
    return t;
}

// 命令行入口，返回是否已处理
static void cal_cli(String cmd) {
    cmd.trim();

    if (cmd == "cal list") {
        if (cal_count == 0) {
            Serial.println("No active calibration");
            return;
        }
        for (int i = 0; i < cal_count; i++) {
            Serial.printf("ID: %d, Range: [%.1f, %.1f]C, Gain: %.3f, Offset: %.2f\n",
                          i + 1,
                          cal_points[i].lower, cal_points[i].upper,
                          cal_points[i].gain, cal_points[i].offset);
        }
        return;
    }

    if (cmd.startsWith("cal add ")) {
        // cal add <lower> <upper> <offset> <gain>
        float lower = 0, upper = 0, offset = 0, gain = 1;
        int n = sscanf(cmd.c_str(), "cal add %f %f %f %f", &lower, &upper, &offset, &gain);
        if (n != 4) {
            Serial.println("Error: Invalid arguments. Usage: cal add lower upper offset gain");
            return;
        }
        if (upper <= lower) {
            Serial.println("Error: upper must be greater than lower");
            return;
        }
        if (cal_count >= MAX_CAL_POINTS) {
            Serial.printf("Error: Maximum %d calibration points allowed\n", MAX_CAL_POINTS);
            return;
        }
        for (int i = 0; i < cal_count; i++) {
            if (!(upper < cal_points[i].lower || lower > cal_points[i].upper)) {
                Serial.printf("Error: range overlaps calibration ID %d\n", i + 1);
                return;
            }
        }
        cal_points[cal_count].lower = lower;
        cal_points[cal_count].upper = upper;
        cal_points[cal_count].offset = offset;
        cal_points[cal_count].gain = gain;
        cal_count++;
        cal_save();
        Serial.printf("OK: added calibration ID %d\n", cal_count);
        return;
    }

    if (cmd.startsWith("cal del ")) {
        int id = atoi(cmd.c_str() + 8);
        if (id < 1 || id > cal_count) {
            Serial.println("Error: invalid calibration ID");
            return;
        }
        for (int i = id - 1; i < cal_count - 1; i++) {
            cal_points[i] = cal_points[i + 1];
        }
        cal_count--;
        cal_save();
        Serial.printf("OK: deleted calibration ID %d\n", id);
        return;
    }

    if (cmd == "cal") {
        Serial.println("cal list | cal add <lower> <upper> <offset> <gain> | cal del <id>");
        return;
    }

    Serial.println("Unknown cal command. Use 'cal' for help.");
}
