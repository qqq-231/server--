//
// Created by zhao2 on 2025/11/22.
//

#include "../../include/chunker/n_transform_gear.h"
#include "../../include/chunker/gear_poly.h"
#include "../../include/const_var.h"
#include "../../include/MA.h"

NTransform_gear::NTransform_gear(uint64_t super_feature_per_chunk, uint64_t feature_per_chunk,
    uint64_t feature_per_super_feature) {

    super_feature_per_chunk_ = super_feature_per_chunk;
    feature_per_chunk_ = feature_per_chunk;
    feature_per_super_feature_ = feature_per_super_feature;

    gear_util_ = new GearHashUtil(config.GetSimilarSlidingWinSize(), config.GetMask());
}

NTransform_gear::~NTransform_gear() {
    delete gear_util_;
}

void NTransform_gear::ExtractFeature(GearCtx_t& ctx, uint8_t* data, uint32_t size,
    uint64_t* features) {

    gear_util_->ResetCtx(ctx);

    std::vector<uint32_t> sub_features(feature_per_chunk_, 0);

    uint64_t finger_print = 0;
    for (int i = 0; i < size; i++) {
        uint8_t* cur_data = data + i;
        finger_print = gear_util_->SlideOneByte(ctx, *cur_data);

        for (int j = 0; j < feature_per_chunk_; j++) {
            const uint32_t transform = (M[j] * finger_print + A[j]);
            if (sub_features[j] >= transform || 0 == sub_features[j])
                sub_features[j] = transform;
        }
    }

    auto hash_buf = (const uint8_t* const)(sub_features.data());
    for (int i = 0; i < super_feature_per_chunk_; i++) {
        uint64_t hash_value = 0;
        auto this_hash_buf = hash_buf + i * feature_per_super_feature_ * sizeof(uint32_t);
        for (int j = 0; j < feature_per_super_feature_ * sizeof(uint32_t); j++) {
            hash_value = (hash_value << 1) + GEAR_TABLE[this_hash_buf[j]];
        }
        features[i] = hash_value;
    }

    gear_util_->ResetCtx(ctx);
}
