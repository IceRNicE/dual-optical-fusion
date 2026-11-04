#ifndef DRAW_H
#define DRAW_H

#include <Arduino.h>
#include "screen.hpp"
#include "shared_val.hpp"
#include "color_map.hpp"
#include "BilinearInterpolation.hpp"
#include "mlx_drivers/mlx_probe.hpp"
#include "fusion.hpp"

const int biox = 32;
const int bioy = 24;
// 使用 pushImage 渲染整帧
// uint16_t frameBuffer[biox * PROB_SCALE * bioy * PROB_SCALE]; // 320x240 帧缓冲区 - 移到 PSRAM
const int lines = 3;  // 一次渲染多少行的像素
uint16_t  lineBuffer[32 * 10 * lines]; // Toggle buffer for lines
uint16_t  dmaBuffer1[32 * 10 * lines]; // Toggle buffer for lines
uint16_t  dmaBuffer2[32 * 10 * lines]; // Toggle buffer for lines
uint16_t* dmaBufferPtr = dmaBuffer1;
bool dmaBufferSel = 0;


// 绘制十字
inline void draw_cross(int x, int y, int len){
   tft.drawLine(x - len/2, y, x + len/2, y, tft.color565(255, 255, 255));
   tft.drawLine(x, y-len/2, x, y+len/2,  tft.color565(255, 255, 255));

   tft.drawLine(x - len/4, y, x + len/4, y, tft.color565(0, 0, 0));
   tft.drawLine(x, y-len/4, x, y+len/4,  tft.color565(0, 0, 0));
}

// 点测温功能：《坐标按当前传感器的行列数换算，兼容 90640/90641》
inline void show_local_temp(int x, int y, int cursor_size){
   draw_cross(x, y, 8);
   float* tempBuffer = (float*)pReadBuffer;
   if (tempBuffer == nullptr) return;
   int cols = mlx_cols();
   int rows = mlx_rows();
   int scale = (int)mlx_scale();
   int px = x / scale;
   int py = y / scale;
   if (px < 0) px = 0;
   if (py < 0) py = 0;
   if (px >= cols) px = cols - 1;
   if (py >= rows) py = rows - 1;
   // 屏幕行是上下翻转的，取温度矩阵时要反着算
   float temp_xy = tempBuffer[(rows - 1 - py) * cols + px];
   int shift_x, shift_y;
   if (x < 140) {shift_x = 10;} else {shift_x = -60;}
   if (y < 120) {shift_y = 10;} else {shift_y = -20;}
   tft.setTextSize(2);
   tft.setCursor(x + shift_x, y + shift_y);
   tft.printf("%.2f", temp_xy);
}

// 点测温功能
inline void show_local_temp(int x, int y){
   show_local_temp(x, y, 2);
}



void draw(){
   static int value;
   int now_y = 0;
   int cols = mlx_cols();
   int rows = mlx_rows();

   int scale = (int)mlx_scale();
   int render_w = cols * scale;
   int render_h = rows * scale;
   // 图像的渲染流程
   if(use_upsample){
      // 使用双线性插值渲染整帧到缓冲区
      init_interp_tables(cols, rows, scale);
      tft.startWrite();
      for(int y=0; y<rows * scale; y++){
         for(int x=0; x<cols * scale; x++){
            value = bio_linear_interpolation(x, render_h - 1 - y, mlx90640To_buffer, cols, rows);
            lineBuffer[x + now_y * render_w] = colormap[value];
         }
         now_y++;
         if (now_y == lines) {
            dmaBufferPtr = dmaBufferSel ? dmaBuffer2 : dmaBuffer1;
            dmaBufferSel = !dmaBufferSel;
            // 直接画在 (0, y-now_y+1) 位置，宽 render_w
            tft.pushImage(0, y - now_y + 1, render_w, lines, lineBuffer);
            now_y = 0;
         }
      }
      if (now_y != 0) {
         dmaBufferPtr = dmaBufferSel ? dmaBuffer2 : dmaBuffer1;
         dmaBufferSel = !dmaBufferSel;
         tft.pushImage(0, render_h - now_y, render_w, now_y, lineBuffer);
         now_y = 0;
      }
      tft.endWrite();
   }else{
      static uint16_t c565;
      tft.startWrite();
      for (int i = 0; i < rows ; i++){
         for (int j = 0; j < cols; j++){
            c565 = colormap[mlx90640To_buffer[(rows - 1 - i)*cols + j]];
            tft.fillRect(j * scale, (i * scale), scale, scale, c565);
         }
      }
      tft.endWrite();
   }
   // 热点追踪十字由 screen_loop 在状态栏之后统一绘制
   // 绘制温度采集光标
   if (flag_show_cursor==true) {show_local_temp(test_point[0], test_point[1]);}
}

// 绘制热点追踪十字（坐标来自每帧算出的最高温位置）
// offset_x/offset_y 供画中画等带偏移的布局使用
inline void draw_trace_cross(int offset_x = 0, int offset_y = 0) {
   if (!flag_trace_max || !flag_sensor_ok) return;
   int scale = (int)mlx_scale();
   int rows = mlx_rows();
   int cx = offset_x + y_max * scale;
   int cy = offset_y + (rows - 1 - x_max) * scale;
   draw_cross(cx, cy, 8);
}


// 探头准备期间的渲染管线
void preparing_loop(){
   tft.setRotation(1);
   if (prob_status == PROB_CONNECTING){
      tft.setCursor(40, 180);
      tft.setTextColor(TFT_WHITE, TFT_BLACK, true);
      tft.setTextSize(1);
      tft.fillRect(40, 180, 220, 30, TFT_BLACK); 
      tft.printf("Triying to connect to MLX...");
      tft.setCursor(80, 190);
      tft.printf("address: %d\n", 0x33);
      delay(10);
   }else if(prob_status == PROB_INITIALIZING){
      tft.setCursor(40, 180);
      tft.setTextColor(TFT_WHITE, TFT_BLACK, true);
      tft.setTextSize(1);
      tft.fillRect(40, 180, 200, 30, TFT_BLACK); 
      tft.printf("MLX... is ready, initializing...\n");
      delay(10);
   }else if(prob_status == PROB_PREPARING){
      tft.setCursor(40, 180);
      tft.setTextColor(TFT_WHITE, TFT_BLACK, true);
      tft.setTextSize(1);
      tft.fillRect(40, 180, 200, 30, TFT_BLACK); 
      tft.printf("MLX... initializing... \n"); 
      delay(10);
   }
}

// 准备期间的渲染管线
void refresh_status(uint8_t tp_status){
   if (tp_status == TP_CONNECTING){
      tft.setCursor(40, 180);
      tft.setTextColor(TFT_WHITE, TFT_BLACK, true);
      tft.setTextSize(1);
      tft.fillRect(40, 180, 220, 30, TFT_BLACK); 
      tft.printf("prepering touch panel...");
      delay(10); 
   }else if (tp_status == TP_READY){
      tft.setCursor(40, 180);
      tft.setTextColor(TFT_WHITE, TFT_BLACK, true);
      tft.setTextSize(1);
      tft.fillRect(40, 180, 220, 30, TFT_BLACK); 
      tft.printf("touch panel connected...");
      delay(10); 
   }else if (tp_status == TP_NOTFOUND){
      tft.setCursor(40, 180);
      tft.setTextColor(TFT_WHITE, TFT_BLACK, true);
      tft.setTextSize(1);
      tft.fillRect(40, 180, 220, 30, TFT_BLACK); 
      tft.printf("touch panel notfound...");
      delay(10); 
   }
}

// 当前模式标签：Model1=融合，Model2=纯摄像头，Model3=纯热成像
inline const char* get_mode_label() {
   switch (current_display_mode) {
      case MODE_THERMAL_OVERLAY: return "Model1";
      case MODE_CAMERA_ONLY:     return "Model2";
      case MODE_THERMAL_ONLY:    return "Model3";
      default:                   return "Model?";
   }
}

// 电池图标：18x9 外框 + 按电量填充
inline void draw_battery_icon(int x, int y, int percent) {
   if (percent < 0) percent = 0;
   if (percent > 100) percent = 100;
   tft.drawRect(x, y, 18, 9, TFT_WHITE);
   tft.fillRect(x + 18, y + 3, 2, 3, TFT_WHITE);   // 正极触点
   int fill_w = (16 * percent) / 100;
   if (fill_w > 0) {
      tft.fillRect(x + 1, y + 1, fill_w, 7, percent > 20 ? TFT_GREEN : TFT_RED);
   }
}

// 底部状态栏（双行，32px）：上行 = 模式 / 电量 / 帧率，下行 = 三个温度极值
void draw_status_bar(unsigned long dt) {
   tft.fillRect(0, 208, 320, 32, TFT_BLACK);
   tft.setTextSize(1);
   tft.setTextColor(TFT_WHITE, TFT_BLACK);

   // ---- 第一行 ----
   tft.setCursor(4, 210);
   tft.print(get_mode_label());

   draw_battery_icon(228, 210, vbat_percent);
   tft.setCursor(252, 210);
   tft.printf("%d%%", vbat_percent);

   int fps = (dt > 0) ? (int)(1000 / dt) : 0;
   tft.setCursor(288, 210);
   tft.printf("%dfps", fps);

   // ---- 第二行 ----
   tft.setCursor(4, 224);
   tft.printf("MAX %.1fC", T_max_fp);
   tft.setCursor(112, 224);
   tft.printf("MIN %.1fC", T_min_fp);
   tft.setCursor(220, 224);
   tft.printf("AVG %.1fC", T_avg_fp);
}

// 处理tft_espi渲染管线
// 纯热成像模式的最高温十字（画在状态栏之后，避免被状态栏盖住）
inline void draw_trace_cross_thermal() {
   if (!flag_trace_max) return;
   int scale = (int)mlx_scale();
   int rows = mlx_rows();
   int cx = disp_y * scale;
   int cy = (rows - 1 - disp_x) * scale;
   draw_cross(cx, cy, 8);

   char buf[12];
   snprintf(buf, sizeof(buf), "%.1f", T_max_fp);
   int tx = cx + 8;
   int ty = cy - 14;
   if (tx > 320 - 36) tx = cx - 40;
   if (ty < 2) ty = cy + 6;
   tft.setTextSize(1);
   tft.setTextColor(TFT_WHITE, TFT_BLACK);
   tft.setCursor(tx, ty);
   tft.print(buf);
}

void screen_loop(){
   static unsigned long dt = 0;
   dt = millis();

   // 仅摄像头模式：跳过所有 MLX 数据处理
   if (current_display_mode == MODE_CAMERA_ONLY) {
      draw_fusion();
      return;
   }

   // 等待探头写入完成，然后拷贝一帧温度并映射到色表
   while (prob_lock == true) {delay(5);}
   pix_cp_lock = true;
   float* tempBuffer = (float*)pReadBuffer;
   int pixel_count = mlx_pixel_count();
   for (int i = 0; i < pixel_count; i++) {
      int temp_val = (int)(180.0 * (tempBuffer[i] - T_min_fp) / (T_max_fp - T_min_fp));
      if (temp_val < 0) temp_val = 0;       // 下限保护
      if (temp_val > 179) temp_val = 179;   // 上限保护
      mlx90640To_buffer[i] = (uint16_t)temp_val;
   }
   // 与这帧温度一起快照最高温坐标，渲染统一用快照值，
   // 避免"画出来的十字"和"屏幕上的画面"来自不同帧
   disp_x = x_max;
   disp_y = y_max;
   pix_cp_lock = false;

   if (current_display_mode == MODE_THERMAL_ONLY) {
      draw();
   } else {
      draw_fusion();
   }

   dt = millis() - dt;
   if (current_display_mode == MODE_THERMAL_ONLY) {
      draw_status_bar(dt);
      draw_trace_cross_thermal();
   }
   while (cmap_loading_lock == true) {delay(1);}
}



#endif
