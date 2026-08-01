//
// Created by zhao on 2025/11/6.
//

#include "../../include/chunker/gear_poly.h"
#include "../../include/gear.h"

GearHashUtil::GearHashUtil(uint64_t window_size, uint64_t chunk_mask)
    : gear_table_(GEAR_TABLE),
      window_size_(window_size),
      mask_(chunk_mask) {
}

void GearHashUtil::NewCtx(GearCtx_t& ctx) {
    ctx.hash = 0;
}

void GearHashUtil::FreeCtx(GearCtx_t& ctx) {
}

void GearHashUtil::ResetCtx(GearCtx_t& ctx) {
    ctx.hash = 0;
}

uint64_t GearHashUtil::SlideOneByte(GearCtx_t& ctx, uint8_t input_byte) {
    ctx.hash = (ctx.hash << 1) + gear_table_[input_byte];
    return ctx.hash;
}

bool GearHashUtil::IsChunkBoundary(uint64_t hash) {
    return (hash & mask_) == 0;
}

uint32_t GearHashUtil::GetWindowSize() {
    return window_size_;
}
