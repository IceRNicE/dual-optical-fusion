#pragma once
#include<Arduino.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_camera.h"
#include "screen.hpp"
#include "shared_val.hpp"
#include <Wire.h>
// 摄像头在这块板子上的引脚定义
#define CAM_WIDTH 320
#define CAM_HEIGHT 240
#define PWDN_GPIO_NUM     17
#define RESET_GPIO_NUM    26
#define XCLK_GPIO_NUM     14
#define PCLK_GPIO_NUM     22
#define SIOD_GPIO_NUM     23
#define SIOC_GPIO_NUM     18
#define VSYNC_GPIO_NUM    21
#define HREF_GPIO_NUM     27
#define D0_GPIO_NUM       34
#define D1_GPIO_NUM       33
#define D2_GPIO_NUM       25
#define D3_GPIO_NUM       35
#define D4_GPIO_NUM       39
#define D5_GPIO_NUM       38
#define D6_GPIO_NUM       37
#define D7_GPIO_NUM       36
#define I2CPULL_UP  19
#define I2CPULL_UP_OPEN  LOW

bool camera_ok = false;
camera_fb_t *fb = NULL;

// 摄像头电平初始化
void camera_hard_reset() {
    pinMode(I2CPULL_UP, OUTPUT);
    digitalWrite(I2CPULL_UP, I2CPULL_UP_OPEN);
    delay(200);
    pinMode(PWDN_GPIO_NUM, OUTPUT);
    digitalWrite(PWDN_GPIO_NUM, HIGH);
    delay(100);
    digitalWrite(PWDN_GPIO_NUM, LOW);
    delay(200);
}

void camera_init(){
    // 关闭欠压检测
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
    Serial.println("[Camera] step1: hard reset");
    // 摄像头上电
    camera_hard_reset();
    pinMode(I2CPULL_UP, OUTPUT);
    digitalWrite(I2CPULL_UP, I2CPULL_UP_OPEN);
    // 等待摄像头上电
    delay(800);
    Serial.println("[Camera] step2: build config");
    // check_camera_i2c();
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = D0_GPIO_NUM;
    config.pin_d1 = D1_GPIO_NUM;
    config.pin_d2 = D2_GPIO_NUM;
    config.pin_d3 = D3_GPIO_NUM;
    config.pin_d4 = D4_GPIO_NUM;
    config.pin_d5 = D5_GPIO_NUM;
    config.pin_d6 = D6_GPIO_NUM;
    config.pin_d7 = D7_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 8000000;   // OV7670 在 10MHz 下容易出现 DMA 溢出，降到 8MHz
    config.frame_size = FRAMESIZE_QVGA;  // 320*240
    // 本机摄像头是 OV7670（PID 0x0076），不支持 JPEG 输出，只能用 RGB565
    config.pixel_format = PIXFORMAT_RGB565;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.jpeg_quality = 15;
    // 让摄像头使用psram来存储摄像头的数据
    if (psramFound()){
        Serial.printf("PSRAM found, using it for camera buffers\n");
        config.fb_count = 2;
        config.grab_mode = CAMERA_GRAB_LATEST;
    }
    else{   
        Serial.printf("PSRAM not found\n");
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_DRAM;
    }
    Serial.println("[Camera] step3: esp_camera_init");
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
        Serial.printf("Camera init failed with error: 0x%x\n", err);
        camera_ok = false;
        esp_camera_deinit();
        return;
    }
    camera_no_jpeg = true;
    camera_ok = true;
    Serial.println("Camera init okay");

    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        Serial.printf("[Camera] Sensor PID=0x%04X\n", s->id.PID);
        // 注意：OV7670 的 set_vflip()/set_hmirror() 实测无效，
        // 翻转统一在 camera_grab_rgb565() 里用软件完成，这里不再调用。
        if (s->id.PID == OV3660_PID) {
            s->set_brightness(s, 1);
            s->set_saturation(s, -2);
        }
    }
}


void screen_draw_jpeg(const uint8_t* jpg_data, size_t len){
    if (jpg_data == nullptr || len == 0) return;
    TJpgDec.setJpgScale(1);
    TJpgDec.setCallback(tft_output);
    tft.startWrite();
    TJpgDec.drawJpg(0, 0, jpg_data, len);
    tft.endWrite();
}

void camera_loop(){ 
    if (!camera_ok) {
        return;
    }
        // 1. 获取 JPEG 帧
    fb = esp_camera_fb_get();
    if (!fb) {
        Serial.printf("Camera capture failed\n");
        esp_camera_fb_return(fb); // 即使失败也要尝试归还
        fb = NULL;
        return;
    }
     if (fb->format == PIXFORMAT_JPEG) {
        // 2. 配置解码器
        TJpgDec.setJpgScale(1); // 1:1 解码，不缩放
        TJpgDec.setSwapBytes(false); // 交换字节序 (Big/Little Endian)
        screen_draw_jpeg(fb->buf, fb->len); // 画 JPEG 到屏幕
    } else {
        Serial.println("Non-JPEG frame received!");
    }
    // 6. 释放摄像头帧缓冲
    esp_camera_fb_return(fb);
    fb = NULL;
}