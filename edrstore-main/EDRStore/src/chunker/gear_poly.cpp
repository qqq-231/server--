//
// Created by zhao on 2025/11/6.
//

#include "../../include/chunker/gear_poly.h"
//#include "../../include/const_var.h"
#include "../../include/gear.h"

// 在某个源文件中定义 Gear 表（比如 gear_hash.cc）
// const uint64_t GEAR_TABLE[] = {
//     0xe80e8d55032474b3, 0x11b25b61f5924e15, 0x03aa5bd82a9eb669,
//     // ... 你提供的完整表数据
//     0x9d714428471c65fe,
// };

GearHashUtil::GearHashUtil(uint64_t window_size, uint64_t chunk_mask)
    : gear_table_(GEAR_TABLE),  // 直接指向预定义表
      window_size_(window_size),
      mask_(chunk_mask) {

    // 构造函数变得非常简单，只需要初始化成员
    // 不需要任何动态分配或复杂计算
}

// 其他成员函数保持不变
void GearHashUtil::NewCtx(GearCtx_t& ctx) {
  //  ctx.circ_buf = (uint8_t*)malloc(window_size_ * sizeof(uint8_t));
  //  memset(ctx.circ_buf, 0, window_size_);
    ctx.cur_pos = -1;
    ctx.hash = 0;
}

void GearHashUtil::FreeCtx(GearCtx_t& ctx) {
  //  free(ctx.circ_buf);
    //============================
    //ctx.circ_buf = nullptr;
}

void GearHashUtil::ResetCtx(GearCtx_t& ctx) {
   // memset(ctx.circ_buf, 0, window_size_);
   // ctx.cur_pos = -1;
    ctx.hash = 0;
}

uint64_t GearHashUtil::SlideOneByte(GearCtx_t& ctx, uint8_t input_byte) {
   // ctx.cur_pos = (ctx.cur_pos + 1) % window_size_;
    //uint8_t old_byte = ctx.circ_buf[ctx.cur_pos];
    //ctx.circ_buf[ctx.cur_pos] = input_byte;

    // Keep the original odess_start Gear semantics.  XOR produces a different
    // feature space even when the configured feature name is unchanged.
    ctx.hash = (ctx.hash << 1) + gear_table_[input_byte];
    //ctx.hash = (ctx.hash << 1) ^ gear_table_[input_byte] ^ gear_table_[old_byte];
   // finger_print = (finger_print << 1) + GEAR_TABLE[content[i]];
    return ctx.hash;
}

bool GearHashUtil::IsChunkBoundary(uint64_t hash) {
    return (hash & mask_) == 0;
}
uint32_t GearHashUtil::GetWindowSize()
{
    return window_size_;
}
