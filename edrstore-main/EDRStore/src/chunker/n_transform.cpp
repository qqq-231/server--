//
// Created by zhao2 on 2025/11/22.
//

#include "../../include/chunker/n_transform.h"

#include <cstdint>
#include <vector>
#include <climits>

static uint32_t M[] = {
    0x5b49898a, 0xe4f94e27, 0x95f658b2, 0x8f9c99fc,
    0xeba8d4d8, 0xba2c8e92, 0xa868aeb4, 0xd767df82,
    0x843606a4, 0xc1e70129, 0x32d9d1b0, 0xeb91e53c,
};

static uint32_t A[] = {
    0xff4be8c,  0x6f485986, 0x12843ff,  0x5b47dc4d,
    0x7faa9b8a, 0xd547b8ba, 0xf9979921, 0x4f5400da,
    0x725f79a9, 0x3c9321ac, 0x32716d,   0x3f5adf5d,
};

NTransform::NTransform(uint64_t super_feature_per_chunk,
                       uint64_t feature_per_chunk,
                       uint64_t feature_per_super_feature)
    : super_feature_per_chunk_(super_feature_per_chunk),
      feature_per_chunk_(feature_per_chunk),
      feature_per_super_feature_(feature_per_super_feature) {

    rabin_util_ = new RabinFPUtil(config.GetSimilarSlidingWinSize());
}

NTransform::~NTransform() {
    delete rabin_util_;
}

void NTransform::ExtractFeature(RabinCtx_t& ctx, uint8_t* data, uint32_t size,
                                uint64_t* features) {

    rabin_util_->ResetCtx(ctx);

    std::vector<uint32_t> sub_features(feature_per_chunk_, UINT32_MAX);

    uint64_t finger_print = 0;

    for (uint32_t i = 0; i < size; ++i) {
        finger_print = rabin_util_->SlideOneByte(ctx, data[i]);

        for (uint32_t j = 0; j < feature_per_chunk_; ++j) {
            uint32_t transform = (M[j] * finger_print + A[j]);
            if (transform < sub_features[j]) {
                sub_features[j] = transform;
            }
        }
    }

    for (uint32_t i = 0; i < super_feature_per_chunk_; ++i) {
        uint32_t base_idx = i * feature_per_super_feature_;
        uint8_t* buf = (uint8_t*)(sub_features.data() + base_idx);
        size_t len = sizeof(uint32_t) * feature_per_super_feature_;
        features[i] = XXHash64::hash(buf, len, 0);
    }
}
