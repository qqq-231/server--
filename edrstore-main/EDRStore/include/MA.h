//
// MA.h — 集中定义特征提取算法的 M[] / A[] 变换常数
//
// M[j] * fingerprint + A[j]  = 32-bit 线性变换
// 用于 NTransform / OdessUtil / OdessSimple / OdessStart /
//     New_MinHash / NTransform_gear 六个算法
//
// 数组容量为 32，当前配置只使用前 FEATURE_PER_CHUNK=12 个常量。
// 扩大特征数会改变协议中的数组维度，应与数据格式迁移一起进行。
//

#ifndef EDRSTORE_MA_H
#define EDRSTORE_MA_H

#include <cstdint>

static const uint32_t M[32] = {
    // ========== 原有 12 个 (SUPER=3, SUB=4, TOTAL=12) ==========
    0x5b49898a, 0xe4f94e27, 0x95f658b2, 0x8f9c99fc,
    0xeba8d4d8, 0xba2c8e92, 0xa868aeb4, 0xd767df82,
    0x843606a4, 0xc1e70129, 0x32d9d1b0, 0xeb91e53c,

    // ========== 新增 20 个 (SUPER 3→8, TOTAL 12→32) ==========
    0x7d3c1b9a, 0xf2e80567, 0x4a1f3d2c, 0x9b5e8071,
    0x1c6d3f4a, 0x8529ae10, 0x37b4c9d2, 0xead05836,
    0x6491f7e3, 0x0b2c4d6e, 0x8f7a3b1c, 0x52d4e6f8,
    0xc1a0b293, 0x3e5f7d8a, 0x9162f0b4, 0x2d4c6e80,
    0xb8091a3b, 0x5e6f7c8d, 0xa1b2c3d4, 0x0f1e2d3c,
};

static const uint32_t A[32] = {
    // ========== 原有 12 个 (SUPER=3, SUB=4, TOTAL=12) ==========
    0x0ff4be8c, 0x6f485986, 0x012843ff, 0x5b47dc4d,
    0x7faa9b8a, 0xd547b8ba, 0xf9979921, 0x4f5400da,
    0x725f79a9, 0x3c9321ac, 0x0032716d, 0x3f5adf5d,

    // ========== 新增 20 个 (SUPER 3→8, TOTAL 12→32) ==========
    0x4a1b3c2d, 0x8f6e5d4c, 0x2b3a4958, 0x7c6d5e4f,
    0x1a0b2c3d, 0x6f5e4d3c, 0x9b8a7c6d, 0x0d1e2f3a,
    0x5463a1b2, 0xc8d9e0f1, 0x3f2e1d0c, 0x7b6a5948,
    0x1c3d5e6f, 0x8091a2b3, 0x4d5c6b7a, 0x2e1f0a9b,
    0x63748596, 0xafbec0d1, 0x8c7b6a59, 0x3a2b1c0d,
};

#endif // EDRSTORE_MA_H
