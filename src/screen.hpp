
#pragma once
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <Arduino.h>
#include "splash_jpg.hpp"   // 自定义开屏画面（320x240 JPEG）

#define PIN_BLK 4
extern uint8_t brightness;  // 在 shared_val.hpp 中定义

bool color_reverse = true;  // 是否反转色彩
static const uint16_t screenWidth  = 320;
static const uint16_t screenHeight = 240;

TFT_eSPI tft = TFT_eSPI(screenHeight, screenWidth); /* TFT instance */

// ================= LEDC PWM 配置（高频避免干扰热成像）====================
#define LEDC_CHANNEL    0
#define LEDC_FREQ_HZ    20000  // 20kHz
#define LEDC_RESOLUTION 8      // 8位分辨率 (0-255)

// ================= 亮度控制 =================
inline void set_brightness(int _brightness, bool remenber=true){
   // 首次调用时初始化 LEDC
   static bool ledc_initialized = false;
   if (!ledc_initialized) {
       ledcSetup(LEDC_CHANNEL, LEDC_FREQ_HZ, LEDC_RESOLUTION);
       ledcAttachPin(PIN_BLK, LEDC_CHANNEL);
       ledc_initialized = true;
   }

   if (_brightness < 255 && _brightness > 5){
      ledcWrite(LEDC_CHANNEL, _brightness);
      if (remenber) {brightness = _brightness;}
   }else if(_brightness >= 255) {
        ledcWrite(LEDC_CHANNEL, 255);
        if (remenber){brightness = _brightness;}
   }else if(_brightness <= 5) {
      ledcWrite(LEDC_CHANNEL, 5);
      if(remenber){brightness = _brightness;}
   }
}

// ================= 亮起屏幕 =================
void smooth_on(){
   digitalWrite(PIN_BLK, LOW);
   for(int i=0; i<brightness; i++){
      set_brightness(i, false);
      delay(2);
   }
}

// ================= 熄灭屏幕 =================
void smooth_off(){
   for(int i=brightness; i>=0; i--){
      ledcWrite(LEDC_CHANNEL, i);
      delay(2);
   }
}

// ================= JPEG 解码回调 =================
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap){
    // 用途就是把解码后的图像数据绘制到屏幕上
   if ( y >= tft.height() ) return 0;
   tft.pushImage(x, y, w, h, bitmap);
   return 1;
}

// ================= 绘制开屏画面 =================
void render_splash(){
   TJpgDec.setCallback(tft_output);  // 设置 jpeg 解码回调
   tft.fillScreen(TFT_BLACK);
   TJpgDec.setJpgScale(1);
   tft.startWrite();
   TJpgDec.drawJpg(0, 0, splash_jpg, splash_jpg_len);
   tft.endWrite();
}

// ================= 屏幕初始化主函数 =================
void screen_init(){
    Serial.println("Initializing screen...");
    // 初始化屏幕背光的控制引脚
    pinMode(PIN_BLK, OUTPUT);
    digitalWrite(PIN_BLK, LOW);
    
    tft.init();
    tft.setRotation(1); 
    // 关闭全局字节交换：实测本机 ST7789 需要 big-endian RGB565 原样发送，
    // 开启 swap 会把所有颜色打成蓝色（红蓝互换）。
    tft.setSwapBytes(false);
    // 注意：本机不能开 invertDisplay。初版代码按 EEPROM[15] 是否为零决定反色，
    // 而这块板子的 EEPROM[15] 恰好是 0，导致屏幕整体变负片（红黄变青蓝）。
    // 这里直接固定为不反色。
    color_reverse = false;
    tft.invertDisplay(false);
    Serial.print("[Screen] Invert disabled (normal colors).");

    // 打开屏幕之后要做的事情：慢慢打开屏幕，然后绘制开屏画面
    render_splash();
    delay(300);
    smooth_on();
    delay(500);
    smooth_off();
}

// 支持的命令：screen off, screen on, screen brightness <value>
void screen_cli(String cmd){
    cmd.trim();
    if (cmd.equalsIgnoreCase("screen off")){
        smooth_off();
        // 增加熄屏回显
        Serial.println("[Screen] Turned OFF smoothly.");
        
    }else if(cmd.equalsIgnoreCase("screen on")){
        smooth_on();
        // 增加亮屏回显
        Serial.println("[Screen] Turned ON smoothly.");
        
    }else if(cmd.startsWith("screen brightness ")){
        String valueStr = cmd.substring(String("screen brightness ").length());
        int value = valueStr.toInt();
        set_brightness(value);
        // 增加亮度设置回显，把实际设置的值打印出来
        Serial.print("[Screen] Brightness set to: ");
        Serial.println(value);
        
    }else if (cmd.startsWith("color_reverse ")){
        String param = cmd.substring(14);
        param.trim();  // 去除参数两端的空白字符
        if (param == "1") {
            color_reverse = true; // 设置为反转
            Serial.println("Color reverse enabled.");
            delay(100);
            tft.invertDisplay(false); // 立即应用反转设置
            EEPROM.write(15, color_reverse);
            EEPROM.commit(); // 确保数据写入 EEPROM
        } else if (param == "0") {
            color_reverse = false; // 设置为不反转
            Serial.println("Color reverse disabled.");
            delay(100);
            tft.invertDisplay(true); // 立即应用反转设置
            EEPROM.write(15, color_reverse);
            EEPROM.commit(); // 确保数据写入 EEPROM
        } else if (param == "-q") {
            Serial.println("Color reverse status: " + String(color_reverse ? "enabled" : "disabled")); // 查询当前状态
        } else if (param == "-r") {
            color_reverse = !color_reverse; // 切换反转状态
            delay(100);
            tft.invertDisplay(!color_reverse);
            EEPROM.write(15, color_reverse);
            EEPROM.commit(); // 确保数据写入 EEPROM
        }else {
            Serial.println("Invalid parameter for color_reverse. Use '1' to enable, '0' to disable, or '-q' to query status.");
        }
    }else{
        // 未知指令提示
        Serial.println("[Error] Unknown screen command: " + cmd);
    }
}