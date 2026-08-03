//
// Created by zhao2 on 2025/11/18.
//


#include "../../include/chunker/odess_simple.h"
#include "../../include/chunker/gear_poly.h"
#include "../../include/const_var.h"
#include "../../include/MA.h"







/**
 * @brief Construct a new FinesseUtil object
 *
 * @param super_feature_per_chunk super feature per chunk
 * @param feature_per_chunk feature per chunk
 * @param feature_per_super_feature feature per super feature
 */


OdessSimple::OdessSimple(uint64_t super_feature_per_chunk, uint64_t feature_per_chunk,
    uint64_t feature_per_super_feature){

        // 设置特征参数
        super_feature_per_chunk_ = super_feature_per_chunk;//超级特征数量
        feature_per_chunk_ = feature_per_chunk;//总特征数量
        feature_per_super_feature_ = feature_per_super_feature;//每个超级特征包含的子特征数量

        // 初始化 Rabin 指纹工具（使用配置的滑动窗口大小）（窗口大小=48字节） config，json里设置
      gear_util_ = new GearHashUtil(config.GetSimilarSlidingWinSize(),config.GetMask());
        //参数关系：feature_per_chunk = super_feature_per_chunk * feature_per_super_feature

}



/**
 * @brief Destroy the FinesseUtil object
 *
 */
OdessSimple::~OdessSimple() {
   // delete gear_util_;
}

/**
 * @brief compute the features from the chunk
 *
 * @param ctx the rabin ctx
 * @param data the chunk data
 * @param size the chunk size
 * @param features the chunk features
 */
void OdessSimple::ExtractFeature(GearCtx_t& ctx, uint8_t* data, uint32_t size,
    uint64_t* features) {
//原始数据块的起始地址   原始数据块的大小（字节数）  输出特征数组

    std::vector<uint32_t> sub_features(super_feature_per_chunk_, 0);

 // === 1. 初始化 ===
    gear_util_->ResetCtx(ctx);


    int chunk_length = size;
   // uint8_t *content = data;
    uint64_t finger_print = 0;
    // calculate sub features.
    for (int i = 0; i < chunk_length; i++) {
        uint8_t* cur_data =  data + i ;
        finger_print = gear_util_->SlideOneByte(ctx, *cur_data);


       // finger_print = (finger_print << 1) + GEAR_TABLE[content[i]];
        //----=====---
        if (gear_util_->IsChunkBoundary(finger_print)) {
            for (int j = 0; j < super_feature_per_chunk_ ; j++) {
                const uint32_t transform = (M[j] * finger_print + A[j]);

                if (sub_features[j] >= transform || 0 == sub_features[j])
                    sub_features[j] = transform;
            }
        }

    }







    for (int i = 0; i < super_feature_per_chunk_; i++) {  // 注意这里应该是 super_feature_per_chunk_

        features[i] = sub_features[i];
    }



    // auto hash_buf = reinterpret_cast<const uint8_t*const>(sub_features.data());
    // for (int i = 0; i < super_feature_per_chunk_; i++) {  // 注意这里应该是 super_feature_per_chunk_
    //     uint64_t hash_value = 0;
    //     auto this_hash_buf = hash_buf + i * feature_per_super_feature_ * sizeof(uint32_t);
    //     for (int j = 0; j < feature_per_super_feature_ * sizeof(uint32_t); j++) {
    //         if (j < feature_per_chunk_ * sizeof(uint32_t)) {  // 边界检查
    //             hash_value = (hash_value << 1) + GEAR_TABLE[this_hash_buf[j]];
    //         }
    //     }
    //     features[i] = hash_value;
    // }



    tool::Logging(my_name_.c_str(), "初始化  use odess_simple.\n");

    gear_util_->ResetCtx(ctx);




}


