//
// Created by zhao2 on 2025/11/22.
//

#ifndef EDRSTORE_MAIN_N_TRANSFORM_GEAR_H
#define EDRSTORE_MAIN_N_TRANSFORM_GEAR_H

#include "../../include/chunker/gear_poly.h"
#include "xxhash64.h"
#include "../configure.h"
#include "../../include/define.h"
#include "../gear.h"

using namespace std;

extern Configure config;

class NTransform_gear {
private:
    string my_name_ = "NTransform_gear";

    uint64_t super_feature_per_chunk_;
    uint64_t feature_per_chunk_;
    uint64_t feature_per_super_feature_;

    GearHashUtil* gear_util_;

public:
    NTransform_gear(uint64_t super_feature_per_chunk, uint64_t feature_per_chunk,
         uint64_t feature_per_super_feature);

    ~NTransform_gear();

    void ExtractFeature(GearCtx_t& ctx, uint8_t* data, uint32_t size,
        uint64_t* features);
};

#endif //EDRSTORE_MAIN_N_TRANSFORM_GEAR_H
