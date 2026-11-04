#pragma once

#include <Arduino.h>
#include "shared_val.hpp"
#include "fusion.hpp"
#define BUTTON_PIN 0
#define BTN_LONG_PUSH_T 1000
#define BUTTON_TRIG_LEVEL LOW

void button_init(){
    pinMode(BUTTON_PIN, INPUT_PULLUP);
}

void func_button_long_pushed(){
    // 长按：切换最高温追踪十字的显示
    flag_trace_max = !flag_trace_max;
    Serial.printf("[Button] Trace max: %s\n", flag_trace_max ? "ON" : "OFF");
}

void func_button_pushed(){
    // 短按：切换显示模式（纯热成像 / 摄像头 / 画中画 / 融合）
    next_display_mode();
    Serial.printf("[Button] Display mode: %s\n", get_display_mode_name());
}


void button_loop(){
    static unsigned long btn_pushed_start_time =  0;
    static bool btn_pushed = false;
    static bool btn_long_pushed = false;
    if (digitalRead(BUTTON_PIN) == BUTTON_TRIG_LEVEL){  // 按钮1触发 
        if (millis() - btn_pushed_start_time >= BTN_LONG_PUSH_T){
            if (!btn_long_pushed){
            func_button_long_pushed();
            btn_long_pushed = true;
            }
        }
        vTaskDelay(5);
        if (digitalRead(BUTTON_PIN) == BUTTON_TRIG_LEVEL){btn_pushed=true;}
    }else{
        btn_pushed_start_time = millis();
        if (btn_pushed) {
            if (!btn_long_pushed){func_button_pushed();}
        }
        btn_pushed=false;
        btn_long_pushed = false;
    }
}