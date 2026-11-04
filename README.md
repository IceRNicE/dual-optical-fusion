# ESP32 PlatformIO 实战教程 第五课：双光融合热成像仪的实现与调优

欢迎来到本系列教程的第五课。在前四节课中，我们已经成功跑通了 OV2640 可见光摄像头与 MLX90640 红外热成像传感器，并在屏幕上分别实现了独立渲染。

本节课的核心目标是：**将可见光与红外热力图进行空间级对齐与深度融合 (Dual-Light Fusion)**。这不是简单的两张图片叠加，我们将在算力受限的 ESP32 上，利用 PSRAM 实现**渲染**，并深入底层推导**仿射变换 (Affine Transformation)** 算法来消除双镜头视差。最后，借助 LittleFS 文件系统与串口 CLI，构建一套完整的工业级在线标定系统。

![双光融合最终效果演示](./.readmeimg/lesson5_hero.jpg)

短按boot按钮可以实现拍照的功能，长按boot按钮可以切换不同的显示模式！
- 纯热成像
- 纯可见光
- 融合模式1
- 融合模式2
- 融合模式3

---

## 🎯 本课核心知识点

1. **PSRAM 与 TFT_eSprite 双缓冲**：告别画面撕裂，实现丝滑的多图层全屏渲染。
2. **仿射变换与逆向映射 (Reverse Mapping)**：彻底解决双摄像头物理位置不同导致的“视差”问题。
3. **RGB565 字节序与 Alpha 混合**：深入底层内存，揭秘颜色叠加与大小端转换的细节。
4. **LittleFS 状态持久化**：在板载 Flash 中安全保存标定参数，断电不丢失。

---

## 🧠 核心技术深度解密

本节课的精华全部集中在代码的精妙处理上，让我们深入挖掘 `fusion.hpp` 和底层机制。

### 1. TFT_eSprite 与 PSRAM 缓冲

如果在 LCD 上先画一层热力图，再叠加画一层半透明的摄像头画面，是无法实现的，叠加本质上是一个逐个像素计算的过程。所以，需要一个足够大的空间存储所有的像素，并且在这个空间中进行计算工作。

在 `fusion.hpp` 中，我们利用 ESP32 的片外 PSRAM 创建了一个 320x240 的完整内存画布（Sprite）。
```cpp
// 初始化全屏 Sprite（在 PSRAM 中创建）
fusion_sprite = new TFT_eSprite(&tft);
fusion_sprite->createSprite(320, 240);
```

**渲染流水线为：**
1. 获取摄像头 JPEG 帧，通过 `TJpgDec` 软解码直接写入内存缓冲区 `cam_buffer`。
2. 计算热成像温度，将伪彩色数据渲染到 `fusion_sprite` 内存区。
3. 在内存中将 `cam_buffer` 的像素与 `fusion_sprite` 的像素进行 Alpha 混合。
4. 调用 `fusion_sprite->pushSprite(0, 0);`，利用 DMA 将整块内存一次性推送到屏幕！

### 2. 消除物理视差：增量仿射变换与逆向映射

由于热成像探头和可见光摄像头在 PCB 上的物理中心不重合，视野大小 (FOV) 也不一样，两者的画面天然存在偏差。我们需要对热力图或摄像头画面进行**平移 ($t_x, t_y$)**、**缩放 ($s_x, s_y$)** 和**旋转 ($\theta$)** 操作。


$$
\begin{bmatrix} u \\ v \end{bmatrix} = \begin{bmatrix} \cos\theta & \sin\theta \\ -\sin\theta & \cos\theta \end{bmatrix} \begin{bmatrix} \frac{1}{s_x} & 0 \\ 0 & \frac{1}{s_y} \end{bmatrix} \begin{bmatrix} x - t_x - c_x \\ y - t_y - c_y \end{bmatrix} + \begin{bmatrix} c_{src\_x} \\ c_{src\_y} \end{bmatrix}
$$

如果采用“正向映射”（把原图的像素算好坐标塞进新图），由于缩放操作，目标图像上会留下大量黑色的“空洞”。因此在 `draw_thermal_overlay()` 中，我们采用了**逆向映射 (Reverse Mapping)**：遍历目标屏幕的每一个像素 $(x, y)$，反算回源图像的坐标 $(u, v)$ 去取色。

为了榨干 ESP32 的性能，避免在 $320 \times 240 = 76800$ 次循环中进行极其耗时的矩阵乘法，代码运用了**增量步进算法**：

```cpp
// 预计算逆变换矩阵系数（位于循环外）
float m00 = cos_a * inv_sx;
float m01 = sin_a * inv_sx;
float m10 = -sin_a * inv_sy;
float m11 = cos_a * inv_sy;

// ...
for (int y = 0; y < 240; y++) {
    // 每一行起点的源坐标 (u, v)
    float u_f = start_u + y * m01;
    float v_f = start_v + y * m11;
    
    for (int x = 0; x < 320; x++) {
        int u = (int)u_f;
        int v = (int)v_f;
        // ... (取色与混合逻辑) ...
        
        // 增量步进：同一行内，x 每增加 1，u 和 v 的变化量是一个常数！
        u_f += m00;  // 仅用加法代替了极其耗时的浮点乘法
        v_f += m10;
    }
} 
```
这种将矩阵乘法降维为简单加法的算法，是我们在微控制器上实现高帧率双光融合！


### 3. 底层细节：RGB565 大小端转换与 Alpha 混合

当我们尝试将两个颜色混合时，不能简单地相加。RGB565 格式用 16 bits 紧凑地存储了红绿蓝三个通道（5位红，6位绿，5位蓝）。

在 `alpha_blend` 函数中，我们隐藏了一个极易踩坑的技术细节：**字节序 (Endianness)**。`TFT_eSprite` 内部为了兼容某些屏幕的 SPI 传输要求，默认会把 16位 颜色的高低字节进行互换（大端转小端）。如果在混合时不换回来，颜色就会彻底乱码！

```cpp
inline uint16_t alpha_blend(uint16_t bg_color, uint16_t fg_color, uint8_t alpha) {
    // 1. 还原大小端 (TFT_eSprite 存储的是字节交换后的数据)
    bg_color = (bg_color << 8) | (bg_color >> 8);
    fg_color = (fg_color << 8) | (fg_color >> 8);

    // 2. 提取 RGB 分量 (位操作)
    uint8_t r1 = (bg_color >> 11) & 0x1F;
    uint8_t g1 = (bg_color >> 5) & 0x3F;
    uint8_t b1 = bg_color & 0x1F;
    // ... 提取 fg_color 的分量 ...

    // 3. 应用透明度权重进行混合
    uint8_t r = (r1 * (255 - alpha) + r2 * alpha) / 255;
    // ... 计算 g, b ...

    // 4. 重组为 RGB565，并【再次交换字节序】以存回 Sprite 内存！
    uint16_t result = (r << 11) | (g << 5) | b;
    return (result << 8) | (result >> 8);
}
```

### 4. 固化标定成果：LittleFS 文件系统

每次开机重新调参显然不现实。在 `file_system.hpp` 中，我们初始化了 ESP32 的 Flash 分区并挂载了 `LittleFS`。
当我们在串口调用保存指令时，程序会将对齐参数 `align_tx`, `align_ty` 等浮点数，通过 `reinterpret_cast` 或取地址转为字节流，直接写入 `/align.cfg` 文件中：
```cpp
file.write((uint8_t*)&align_tx, sizeof(float));
// ...
```

即使设备断电，下次 `load_align_params()` 也能精确无误地将字节流还原回 `float`，让你的设备拥有记忆。

---


### 3. 上位机联动：Python 实时配准工具

手动在串口里输入盲猜的浮点数效率太低。我们配套的 `tool.py` 脚本利用 Tkinter 构建了一个虚拟的物理沙盒：
* **橙色虚线框**：代表固定的 240x240 热成像基准画面。
* **绿色实线框**：代表可见光摄像头的画面。

当你在 GUI 中拖拽绿色框时，Python 会实时计算偏移量，并通过 `serial` 库将 `set_align tx ty sx sy ang\n` 指令以 115200 波特率下发给 ESP32。ESP32 侧的 `handle_set_align()` 函数会立刻解析这 5 个参数，更新下一帧的逆向映射矩阵。

这种“软件端模拟视差 - 硬件端实时渲染”的架构，极大降低了标定门槛。


---

## 🚀 实战指南：双光配准操作手册

请确保 ESP32 已经烧录了最新固件，并且通过 USB 线连接到了电脑。

![双光融合最终效果演示](./.readmeimg/image.png)

### 第一步：开启上位机与设备连接
1. 运行上位机工具：`python tool.py`。需要pyserial包，可以通过 `pip install pyserial` 安装。
2. 在左上角“串口连接”下拉菜单中选择 ESP32 对应的端口（如 `COM3` 或 `/dev/cu.usbserial-xxx`）。
3. 状态栏显示“读取成功”后，GUI 界面会同步当前设备内的对齐参数（如果这是首次开机，系统会使用默认值）。

### 第二步：设备端进入融合模式
你可以通过串口工具或者在代码中设定，将设备切换到 `MODE_THERMAL_OVERLAY`（模式 4）。
此时，请找一个具有明显发热特征的参照物（如亮着的老式白炽灯泡、一杯热水，或伸出两根手指），将其放在镜头前约 30-50cm 处。


### 第三步：可视化标定 (GUI 操作)
观察 ESP32 屏幕上的热力图轮廓与可见光轮廓，在 Python 工具中进行盲操对齐：
* **平移对齐**：**鼠标左键**按住画布拖拽。观察屏幕，使绿色框（可见光）的中心平移，直到可见光的物体与热力图的亮斑重叠。
* **缩放对齐**：滑动**鼠标滚轮**。如果发现中心对齐了但边缘合不上，说明两个镜头的 FOV 不一致，通过滚轮放大/缩小可见光画面。
* **旋转对齐**：**鼠标右键**按住画布拖拽。修正由于传感器焊接歪斜导致的轻微旋转角度。

*技巧：可以滑动左侧的“透明度调节”拉杆，将透明度调到 128 左右，能更清晰地看清两个图层的边缘贴合情况。*

### 第四步：持久化保存
当你松开鼠标时，Python 脚本会自动触发 `sync_write_align()` 下发指令。ESP32 接收到指令后，不仅会立刻刷新屏幕，还会调用 `save_align_params()` 将这组珍贵的标定数据写入 LittleFS 的 `/align.cfg` 中。
**标定完成后，哪怕拔掉电源，下次开机设备依然能完美保持对齐状态！**

---

## 📝 完结撒花：未来展望

通过整整 5 节课的硬核实战，我们从零开始打通了硬件 I2C/SPI 总线，实现了传感器的底层驱动；手写了卡尔曼滤波与双线性插值算法；最终在这个资源极其受限的微控制器上，运用图形学和数学魔法，搭配 Python 上位机，打磨出了一款工业级的双光融合热成像仪。

**后续你可以继续魔改的方向：**
* **esp32的双核处理器**可以将绘图与传感器读取分核并行，大幅提升绘图帧率。