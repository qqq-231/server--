//
// Created by zhao on 2025/11/6.
//

#ifndef MY_CODEBASE_GEAR_POLY_H
#define MY_CODEBASE_GEAR_POLY_H
#include <cstdint>

#include "../define.h"
#include "../gear.h"

// 使用预定义的常量 Gear 表
extern const uint64_t GEAR_TABLE[256];



typedef  struct {
    uint32_t cur_pos;
    uint8_t* circ_buf;
    uint64_t hash;
}GearCtx_t;


class GearHashUtil {
private:
    const uint64_t* gear_table_;  // 指向预定义表，不需要动态分配
    uint64_t window_size_;
    uint64_t mask_;

    // 不再需要 InitGearTable() 函数

// public:
//   typedef  struct {
//         uint32_t cur_pos;
//         uint8_t* circ_buf;
//         uint64_t hash;
//     }GearCtx_t;
public:

    // 构造函数简化
    GearHashUtil(uint64_t window_size, uint64_t chunk_mask = 0x0000FFFF);

    // 析构函数简化（不需要释放 gear_table_）
    ~GearHashUtil() = default;

    // 禁用拷贝
    GearHashUtil(const GearHashUtil&) = delete;
    GearHashUtil& operator=(const GearHashUtil&) = delete;

    // 上下文管理
    void NewCtx(GearCtx_t& ctx);
    void FreeCtx(GearCtx_t& ctx);
    void ResetCtx(GearCtx_t& ctx);

    // 核心算法
    uint64_t SlideOneByte(GearCtx_t& ctx, uint8_t input_byte);
    bool IsChunkBoundary(uint64_t hash);
    uint32_t GetWindowSize();
};








#endif //MY_CODEBASE_GEAR_POLY_H