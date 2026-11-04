#ifndef BAT_H
#define BAT_H

#include <Arduino.h>
#include "shared_val.hpp"
#define BAT_ADC  32
float bat_v = 0.;

void bat_init(){
    pinMode(BAT_ADC, INPUT);
}

// 将任意范围的浮点数映射到指定范围的整数
inline int map_float_to_int(float value, float in_min, float in_max, int out_min, int out_max) {
    return (int)((value - in_min) * (out_max - out_min) / (in_max - in_min) + out_min);
}

// 电压 -> 百分比
// 注意：工程默认按 4.2V 满电的锂电映射，但本机实测满电只有约 3.6V
//（官方固件的校准值也是 raw_upper_v=3.550V），所以改用 2.85~3.55V。
inline int map_voltage_to_percentage(float voltage) {
    int p = map_float_to_int(voltage, 2.85f, 3.70f, 0, 100);
    if (p < 0) p = 0;
    if (p > 100) p = 100;
    return p;
}

float get_bat_v(){
    static float r1 = 680.;
    static float r2 = 300.;
    static float coef = (r1+r2) / r2;
    // 多次采样取平均：单次 analogRead 的噪声会让电量数字上下跳。
    uint32_t acc = 0;
    for (int i = 0; i < 8; i++) {
        acc += analogRead(BAT_ADC);
        delayMicroseconds(80);
    }
    bat_v = ((float)acc / 8.0f) / 4096.0f * 3.3f * coef;
    return bat_v;
}

int get_bat_percent(){
    float voltage = get_bat_v();
    return map_voltage_to_percentage(voltage);
}

void bat_loop(){
    static unsigned long lastMillis = 0;
    static const long xWait = 5000;
    if(millis() - lastMillis >= xWait){
      vbat_percent = get_bat_percent();
      lastMillis = millis();
    }
}



#endif