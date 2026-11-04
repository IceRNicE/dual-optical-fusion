// 双线性插值
// By Umeko 2024.08.03

#ifndef BIO_LINEAR_INTERPOLATION_H
#define BIO_LINEAR_INTERPOLATION_H

#include <Arduino.h>
#include <stdint.h>
// ==========================================
// 1. 定点数与查表配置
// ==========================================
#define FP_BITS 10
#define Q ((1 << FP_BITS))

// 最大支持分辨率表大小 (按最大可能的 MLX90640 双倍缩放预留)
// Width: 32 * 18 = 576, Height: 32 * 18 = 576
static int16_t src_x0_table[600];
static int16_t src_fx_table[600];
static int16_t src_y0_table[600];
static int16_t src_fy_table[600];

// 记录当前表格是为哪种配置生成的
static int current_table_w = -1;
static int current_table_h = -1;
static int current_table_scale = -1;

// ==========================================
// 2. 核心实现
// ==========================================

/**
 * @brief 初始化或更新插值表 (懒加载模式)
 * @param src_w 源数据宽度 (Heimann=32, MLX=32/16)
 * @param src_h 源数据高度 (Heimann=32, MLX=24/12)
 * @param scale 放大倍数
 */
inline void init_interp_tables(int src_w, int src_h, int scale){
    // 如果配置没变，直接返回，利用缓存
    if (current_table_w == src_w && current_table_h == src_h && current_table_scale == scale){
        return;
    }

    int dst_w = src_w * scale;
    int dst_h = src_h * scale;

    // 防止越界
    if (dst_w > 600) dst_w = 600;
    if (dst_h > 600) dst_h = 600;

    // 重建 X 轴表
    for (int x = 0; x < dst_w; x++){
        // 映射中心对齐算法: (x + 0.5) / scale - 0.5
        // 定点数优化: ((2*x + 1) * Q) / (2*scale) - Q/2
        // 这里使用简化版: src = x / scale
        int32_t src_x_fp = ((int32_t)x * src_w * Q) / dst_w;
        src_x0_table[x] = (int16_t)(src_x_fp >> FP_BITS);
        src_fx_table[x] = (int16_t)(src_x_fp & (Q - 1));
    }

    // 重建 Y 轴表
    for (int y = 0; y < dst_h; y++){
        int32_t src_y_fp = ((int32_t)y * src_h * Q) / dst_h;
        src_y0_table[y] = (int16_t)(src_y_fp >> FP_BITS);
        src_fy_table[y] = (int16_t)(src_y_fp & (Q - 1));
    }

    current_table_w = src_w;
    current_table_h = src_h;
    current_table_scale = scale;
}

/**
 * @brief 通用双线性插值
 * @param dst_x 屏幕 X
 * @param dst_y 屏幕 Y
 * @param src_data 源数据指针 (扁平化数组)
 * @param src_w 源数据的真实宽度 (用于计算换行 Stride)
 * @param src_h 源数据的真实高度
 */
inline int bio_linear_interpolation(int dst_x, int dst_y, unsigned short *src_data, int src_w, int src_h){
    // 1. 查表
    int src_x0 = src_x0_table[dst_x];
    int src_y0 = src_y0_table[dst_y];
    int fx = src_fx_table[dst_x];
    int fy = src_fy_table[dst_y];

    // 2. 邻居坐标
    int src_x1 = src_x0 + 1;
    int src_y1 = src_y0 + 1;
    
    // 边界钳位
    if (src_x1 >= src_w) src_x1 = src_w - 1;
    if (src_y1 >= src_h) src_y1 = src_h - 1;

    // 3. 计算索引
    // 注意：Heimann 的坐标通常不需要翻转，MLX 需要翻转
    // 为了通用，这里假设 Y 轴不翻转。如果是 MLX，请在传入 dst_y 之前处理，或者在外部翻转数据
    int row0_idx = src_y0 * src_w;
    int row1_idx = src_y1 * src_w;

    int v00 = src_data[row0_idx + src_x0];
    int v01 = src_data[row0_idx + src_x1];
    int v10 = src_data[row1_idx + src_x0];
    int v11 = src_data[row1_idx + src_x1];

    // 4. 插值计算
    int32_t tmp0 = v00 * (Q - fx) + v01 * fx;
    int32_t tmp1 = v10 * (Q - fx) + v11 * fx;
    int32_t numer = tmp0 * (Q - fy) + tmp1 * fy;
    
    return numer >> (2 * FP_BITS);
}

#endif