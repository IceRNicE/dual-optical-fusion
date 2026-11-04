# dual-optical-fusion

基于 ESP32 的双光融合热成像仪固件（热成像 + 可见光摄像头）。

本项目源自 [umeiko/ESP32_Dual_Vision_Thermal](https://github.com/umeiko/ESP32_Dual_Vision_Thermal) 的第 5 课工程（5_duo_vision_90640），并针对一台实际成品机做了硬件适配与功能改造。上游教程见 UPSTREAM_README.md。

## 硬件

| 项目 | 型号 | 备注 |
|---|---|---|
| MCU | ESP32-D0WD | 双核 240MHz，4MB Flash |
| 热成像传感器 | MLX90641 | 16x12，I2C 0x33 |
| 摄像头 | OV7670 | 320x240，RGB565 输出，不支持 JPEG |
| 屏幕 | ST7789 | 320x240，SPI 80MHz |
| 电池 | 单节锂电 | 分压 680k/300k，满电约 3.7V |

上游工程默认假设 MLX90640（32x24）+ OV2640（JPEG），与本机硬件不同，因此驱动层和渲染管线都做了改动。

## 编译

需要 PlatformIO：pio run（编译）、pio run -t upload（烧录）。
依赖：bodmer/TFT_eSPI、bodmer/TJpg_Decoder、espressif32 平台。
分区用 max_app_2MB.csv（app 2MB + spiffs 1.92MB），max_app_4MB.csv 为 4MB 备选。

## 功能

### 三种显示模式（按键短按切换）

- Model1 融合：热成像 50% 半透明覆盖在摄像头上，双线性插值平滑
- Model2 纯摄像头：可见光画面
- Model3 纯热成像：16x12 原始画面直接放大，不做插值

按键长按切换最高温追踪十字的显示。

### 最高温追踪

光标跟随最高温点，带 0.4C 迟滞防抖；十字旁实时显示最高温读数；坐标与温度矩阵同帧快照，避免融合模式下错位。

### 状态栏（底部双行）

    Model1                     [电池] 85% 30fps
    MAX 26.5C   MIN 18.2C   AVG 22.3C

### 温度校准

按区间线性修正 T' = T * gain + offset，最多 16 段，保存在 LittleFS 的 /cal.bin。

## 串口命令（115200）

| 命令 | 说明 |
|---|---|
| cal list | 列出校准区间 |
| cal add <lower> <upper> <offset> <gain> | 添加区间（注意 offset 在 gain 前） |
| cal del <id> | 删除区间 |
| set_align tx ty sx sy ang | 设置配准参数 |
| get_align | 读取配准参数 |
| set_alpha val | 融合透明度 0-255 |
| toggle_vflip / toggle_hflip | 摄像头翻转 |
| mode 0-4 | 直接切换显示模式 |
| bat | 电池电压与百分比 |
| top | 内存占用 |
| ls / df / rm | 文件系统操作 |
| shot | 抓一帧发到 PC（配 shot.py） |

### 校准协议格式

与配套上位机（unified_tool）匹配，格式不能改：

    ID: 1, Range: [20.0, 25.0]C, Gain: 1.020, Offset: 1.50

## 与上游的差异

| 项目 | 上游 | 本项目 |
|---|---|---|
| 传感器 | MLX90640 32x24 | MLX90641 16x12（自动探测 + 失败回退） |
| 摄像头格式 | JPEG（TJpgDec 解码） | RGB565 直采（OV7670 不支持 JPEG） |
| 字节序 | setSwapBytes(true) | false（本机 ST7789 需要大端原样发送） |
| 屏幕反色 | 按 EEPROM[15] 决定 | 强制关闭（旧值导致负片） |
| 拍照 / 画廊 | 有 | 已删除 |
| 显示模式 | 5 种 | 3 种（融合 / 摄像头 / 热成像） |
| 状态栏 | 单行 | 双行（模式 / 电量 / fps / 温度） |
| 融合渲染 | 浮点仿射变换 | 整数半透明叠加 + 双线性插值，约 12fps |
| 电量映射 | 3.55~4.2V | 2.85~3.70V + 8 次采样平均 |

## 目录说明

    platformio.ini          构建配置
    max_app_2MB.csv         分区表（当前使用）
    max_app_4MB.csv         分区表（4MB 备选）
    assets/                 TFT_eSPI 配置（ST7789 引脚、屏幕选择）
    tool.py                 配准调试上位机（独立工具）
    UPSTREAM_README.md      上游教程原文
    src/
      main.cpp              入口：初始化顺序、主循环
      screen.hpp            屏幕初始化、背光、开屏渲染
      draw.hpp              纯热成像渲染、状态栏、最高温十字
      fusion.hpp            融合/画中画渲染、摄像头取帧（RGB565）
      camera.hpp            摄像头初始化（OV7670 RGB565）
      bat.hpp               电池电压采样与百分比
      button.hpp            按键（短按切模式、长按切光标）
      communicate.hpp       串口命令解析
      file_system.hpp       配准参数存取（LittleFS /align.cfg）
      cal.hpp               温度校准（区间 gain/offset）
      shared_val.hpp        全局变量、引脚、EEPROM 地址
      color_map.hpp         180 级色表（Iron 风格）
      BilinearInterpolation.hpp  双线性插值（查表 + 定点运算）
      kalman_filter.hpp     卡尔曼滤波（默认关闭）
      splash_jpg.hpp        开屏画面（320x240 JPEG 数组）
      mlx_drivers/
        MLX90640_API / I2C_Driver   90640 驱动
        MLX90641_API / I2C_Driver   90641 驱动
        mlx_probe.hpp               探测、帧读取、极值统计

## 许可

上游工程为 MIT，本项目改动同样遵循 MIT。
