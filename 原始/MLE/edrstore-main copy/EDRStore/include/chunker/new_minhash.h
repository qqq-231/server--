//
// Created by zhao2 on 2025/12/5.
//

#ifndef EDRSTORE_MAIN_NEW_MINHASH_H
#define EDRSTORE_MAIN_NEW_MINHASH_H

#include "../../include/chunker/gear_poly.h"
#include "xxhash64.h"
#include "../configure.h"
#include "../../include/define.h"
#include "../gear.h"


using namespace std;

extern Configure config;

class New_MinHash {
private:
    string my_name_ = "New_MinHash";

    // configure  配置参数
    uint64_t super_feature_per_chunk_;// 每个数据块的超级特征数量
    uint64_t feature_per_chunk_;// 每个数据块的总特征数量
    uint64_t feature_per_super_feature_; // 每个超级特征包含的基础特征数量

    GearHashUtil* gear_util_;//gear 指纹计算工具


public:
    /**
     * @brief Construct a new FinesseUtil object
     *
     * @param super_feature_per_chunk super feature per chunk
     * @param feature_per_chunk feature per chunk
     * @param feature_per_super_feature feature per super feature
     */
    New_MinHash(uint64_t super_feature_per_chunk, uint64_t feature_per_chunk,
         uint64_t feature_per_super_feature);

    /**
     * @brief Destroy the FinesseUtil object
     *
     */
    ~New_MinHash();

    /**
     * @brief compute the features from the chunk
     *
     * @param ctx the gear ctx
     * @param data the chunk data
     * @param size the chunk size
     * @param features the chunk features
     */
    void ExtractFeature(GearCtx_t & ctx, uint8_t* data, uint32_t size,
        uint64_t* features);
};


#endif //EDRSTORE_MAIN_NEW_MINHASH_H