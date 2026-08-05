//
// Created by zhao2 on 2025/11/22.
//






#include "../../include/chunker/n_transform.h"
#include "../../include/chunker/gear_poly.h"

#include"../../include/const_var.h"


static uint32_t M[] = {
    0x5b49898a, 0xe4f94e27, 0x95f658b2, 0x8f9c99fc, 0xeba8d4d8, 0xba2c8e92,
    0xa868aeb4, 0xd767df82, 0x843606a4, 0xc1e70129, 0x32d9d1b0, 0xeb91e53c,
};

static uint32_t A[] = {
    0xff4be8c,  0x6f485986, 0x12843ff,  0x5b47dc4d, 0x7faa9b8a, 0xd547b8ba,
    0xf9979921, 0x4f5400da, 0x725f79a9, 0x3c9321ac, 0x32716d,   0x3f5adf5d,
};







/**
 * @brief Construct a new FinesseUtil object
 *
 * @param super_feature_per_chunk super feature per chunk
 * @param feature_per_chunk feature per chunk
 * @param feature_per_super_feature feature per super feature
 */


NTransform::NTransform(uint64_t super_feature_per_chunk, uint64_t feature_per_chunk,
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
NTransform::~NTransform() {
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
void NTransform::ExtractFeature(GearCtx_t& ctx, uint8_t* data, uint32_t size,
    uint64_t* features) {
//原始数据块的起始地址   原始数据块的大小（字节数）  输出特征数组


 // === 1. 初始化 ===
    gear_util_->ResetCtx(ctx);

    std::vector<uint32_t> sub_features( feature_per_chunk_ , 0);
    //std::vector<uint64_t> super_features( super_feature_per_chunk_, 0);


    uint64_t finger_print = 0;
    // calculate sub features.
    for (int i = 0; i < size; i++) {
        uint8_t* cur_data =  data + i ;
        finger_print = gear_util_->SlideOneByte(ctx, *cur_data);


        //finger_print = (finger_print << 1) + GEAR_TABLE[content[i]];
        for (int j = 0; j < feature_per_chunk_; j++) {
            const uint32_t transform = (M[j] * finger_print + A[j]);
            // we need to guarantee that when sub_features[i] is not inited,
            // always set its value
            if (sub_features[j] >= transform || 0 == sub_features[j])
                sub_features[j] = transform;
        }
    }

    // group sub features into super features.
    auto hash_buf = (const uint8_t *const)(sub_features.data());
    for (int i = 0; i <super_feature_per_chunk_; i++) {
        uint64_t hash_value = 0;
        auto this_hash_buf = hash_buf + i * feature_per_super_feature_ * sizeof(uint32_t);
        for (int j = 0; j < feature_per_super_feature_ * sizeof(uint32_t); j++) {
            hash_value = (hash_value << 1) + GEAR_TABLE[this_hash_buf[j]];
        }
       features[i] = hash_value;
    }




    //tool::Logging(my_name_.c_str(), "初始化  use N_TransForm 算法.\n");

    gear_util_->ResetCtx(ctx);




}

