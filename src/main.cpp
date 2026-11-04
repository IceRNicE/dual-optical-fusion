#include <Arduino.h>
#include <EEPROM.h>
#include "communicate.hpp"
#include "screen.hpp"
#include "canvas.hpp"
#include "shared_val.hpp"
#include "draw.hpp"
#include "bat.hpp"
#include "camera.hpp"
#include "mlx_drivers/mlx_probe.hpp"
#include "cal.hpp"
#include "fusion.hpp"
#include "button.hpp"
void setup() {
  button_init();
  serial_start();
  cal_load();   // 载入温度校准区间
  bat_init();
  sensor_power_on();
  EEPROM.begin(512);  // 初始化 EEPROM
  screen_init();
  // 全屏画布：所有模式统一渲染到 PSRAM 里这块 320x240 帧缓冲，再一次性推屏。
  // 调试抓帧（串口命令 shot）直接读这块内存。
  canvas_init();
  camera_init();

  // 设置当前传感器类型
  // 本机实测为 MLX90641（16x12）。直接固定，避免旧 EEPROM 值把类型带偏；
  // 如果初始化失败，下面的逻辑会自动切到 90640 并重启重试。
  current_sensor = SENSOR_MLX90641;
  EEPROM.write(EEPROM_ADDR_SENSOR_TYPE, current_sensor);
  EEPROM.commit();
  if (current_sensor == SENSOR_MLX90640) {is_90640 = true;} else {is_90640 = false;}

  // 更新传感器参数
  update_sensor_params();

  // 先用纯热成像模式启动，确认传感器链路正常（融合模式随后再验证）
  current_display_mode = MODE_THERMAL_OVERLAY;
  Serial.printf("[Setup] Display mode: %s (%d)\n",
                get_display_mode_name(), current_display_mode);

  // 尝试初始化 MLX 传感器
  sensor_power_on();
  bool mlx_ok = blocking_mlx_init_and_check(5);

  // 初始化失败时自动切换传感器型号（90640 <-> 90641）并重启重试。
  // 成品机可能装的是 90641（16x12），而工程默认先按 90640（32x24）探测，
  // EEPROM 布局不同会读失败。
  if (!mlx_ok) {
    uint8_t next = (current_sensor == SENSOR_MLX90640) ? SENSOR_MLX90641 : SENSOR_MLX90640;
    Serial.printf("[Setup] MLX init failed, switching sensor type to %d and rebooting...\n", next);
    EEPROM.write(EEPROM_ADDR_SENSOR_TYPE, next);
    EEPROM.commit();
    delay(300);
    ESP.restart();
  }

  if (mlx_ok) {
    flag_sensor_ok = true;
    prob_status = PROB_READY;
    probe_loop_mlx();
    screen_loop();
    Serial.println("[Setup] MLX Sensor initialized successfully");
  } else {
    Serial.println("[Setup] MLX Sensor initialization failed!");
  }

  // 根据传感器和摄像头状态，智能选择显示模式
  bool cam_ok = camera_ok;

  if (!mlx_ok && !cam_ok) {
    // 两者都失败，只能显示准备状态
    current_display_mode = MODE_THERMAL_OVERLAY;  // 使用热成像模式显示准备状态
    Serial.println("[Setup] CRITICAL: Both MLX and Camera failed!");
  } else if (!mlx_ok) {
    // 只有热成像失败，切换到仅摄像头模式
    current_display_mode = MODE_CAMERA_ONLY;
    Serial.println("[Setup] MLX failed, fallback to Camera Only mode");
  } else if (!cam_ok) {
    // 只有摄像头失败，切换到纯热成像模式
    current_display_mode = MODE_THERMAL_OVERLAY;
    Serial.println("[Setup] Camera failed, fallback to Thermal Only mode");
  }
  // 如果两者都正常，保持 EEPROM 中的融合模式

  // 如果是需要摄像头缓冲区的模式，初始化缓冲区
  if (current_display_mode == MODE_PIP_CAMERA) {
    init_fusion_buffers();
  }

  // Alpha 融合模式需要全屏 Sprite
  if (current_display_mode == MODE_THERMAL_OVERLAY) {
    init_fusion_sprite();
  }

  smooth_on();
  Serial.printf("[Setup] Final display mode: %s (%d)\n",
                get_display_mode_name(), current_display_mode);
}

void loop() {
  serial_loop();
  button_loop();
  bat_loop();
  // 根据当前显示模式决定如何渲染
  if (current_display_mode == MODE_CAMERA_ONLY) {
    // 仅摄像头模式：不需要 MLX 传感器
    screen_loop();  // 渲染屏幕（摄像头画面）
  }
  else if (flag_sensor_ok) {
    // 需要 MLX 的模式：读取传感器数据并渲染
    probe_loop_mlx();
    screen_loop();
  }
  else {
    // MLX 未准备好，显示准备状态
    preparing_loop();
    delay(100);
  }
}