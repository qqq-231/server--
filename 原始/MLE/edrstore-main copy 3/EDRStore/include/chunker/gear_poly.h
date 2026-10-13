//
// Created by zhao on 2025/11/6.
//

#ifndef MY_CODEBASE_GEAR_POLY_H
#define MY_CODEBASE_GEAR_POLY_H
#include <cstdint>

#include "../define.h"
#include "../gear.h"

extern const uint64_t GEAR_TABLE[256];


typedef struct {
    uint64_t hash;
} GearCtx_t;


class GearHashUtil {
private:
    const uint64_t* gear_table_;
    uint64_t window_size_;
    uint64_t mask_;

public:

    GearHashUtil(uint64_t window_size, uint64_t chunk_mask = 0x0000FFFF);

    ~GearHashUtil() = default;

    GearHashUtil(const GearHashUtil&) = delete;
    GearHashUtil& operator=(const GearHashUtil&) = delete;

    void NewCtx(GearCtx_t& ctx);
    void FreeCtx(GearCtx_t& ctx);
    void ResetCtx(GearCtx_t& ctx);

    uint64_t SlideOneByte(GearCtx_t& ctx, uint8_t input_byte);
    bool IsChunkBoundary(uint64_t hash);
    uint32_t GetWindowSize();
};


#endif //MY_CODEBASE_GEAR_POLY_H
