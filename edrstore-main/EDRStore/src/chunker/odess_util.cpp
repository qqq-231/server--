//
// Created by zhao on 2025/11/4.
//


#include "../../include/chunker/gear_poly.h"
#include "../../include/chunker/odess_util.h"
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


OdessUtil::OdessUtil(uint64_t super_feature_per_chunk, uint64_t feature_per_chunk,
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
OdessUtil::~OdessUtil() {
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
void OdessUtil::ExtractFeature(GearCtx_t& ctx, uint8_t* data, uint32_t size,
    uint64_t* features) {
//原始数据块的起始地址   原始数据块的大小（字节数）  输出特征数组

    std::vector<uint32_t> sub_features(feature_per_chunk_, 0);

 // === 1. 初始化 ===
    gear_util_->ResetCtx(ctx);


    int chunk_length = size;
    uint8_t *content = data;
    uint64_t finger_print = 0;
    // calculate sub features.
    for (int i = 0; i < chunk_length; i++) {
        uint8_t* cur_data =  data + i ;
        finger_print = gear_util_->SlideOneByte(ctx, *cur_data);


       // finger_print = (finger_print << 1) + GEAR_TABLE[content[i]];
        if (gear_util_->IsChunkBoundary(finger_print) ) {
            for (int j = 0; j < feature_per_chunk_ ; j++) {
                const uint32_t transform = (M[j] * finger_print + A[j]);
                // we need to guarantee that when sub_features[i] is not inited,
                // always set its value
                if (sub_features[j] >= transform || 0 == sub_features[j])
                    sub_features[j] = transform;
            }
        }
       // gear_util_->ResetCtx(ctx);
    }
   // gear_util_->ResetCtx(ctx);

// for (uint32_t i = 0; i < super_feature_per_chunk_; i++)
// {
//     // 对超级特征向量进行哈希
//     features[i] = XXHash64::hash((uint8_t*)super_features[i].data(),
//                                  sizeof(uint64_t) * feature_per_super_feature_, 0);
// }
//


    auto hash_buf = reinterpret_cast<const uint8_t*const>(sub_features.data());
    for (int i = 0; i < super_feature_per_chunk_; i++) {  // 注意这里应该是 super_feature_per_chunk_
        uint64_t hash_value = 0;
        auto this_hash_buf = hash_buf + i * feature_per_super_feature_ * sizeof(uint32_t);
        for (int j = 0; j < feature_per_super_feature_ * sizeof(uint32_t); j++) {
            if (j < feature_per_chunk_ * sizeof(uint32_t)) {  // 边界检查
                hash_value = (hash_value << 1) + GEAR_TABLE[this_hash_buf[j]];
            }
        }
        features[i] = hash_value;
    }







    //
    // auto hash_buf = reinterpret_cast<const uint8_t*const>(sub_features.data());
    // for (int i = 0; i < feature_per_chunk_; i++) {
    //     uint64_t hash_value = 0;
    //     auto this_hash_buf = hash_buf + i * feature_per_super_feature_  * sizeof(uint32_t);
    //     for (int j = 0; j < feature_per_super_feature_  * sizeof(uint32_t); j++) {
    //         hash_value = (hash_value << 1) + GEAR_TABLE[this_hash_buf[j]];
    //     }
    //     features[i] = hash_value;
    // }



   // tool::Logging(my_name_.c_str(), "初始化  use odess.\n");

    gear_util_->ResetCtx(ctx);

//     uint64_t sub_chunk_size;
//     uint32_t last_chunk_size;
//     uint32_t running_limit;
//
//       // 计算子块大小
//     sub_chunk_size = size / feature_per_chunk_;
//     last_chunk_size = sub_chunk_size + (size % feature_per_chunk_);
//
//  // === 2. 数据块分组处理 ===
//     uint64_t tmp_rabin_fp = 0;
//     uint64_t max_rabin_fp = 0;
//     vector<vector<uint64_t>> group_features;
//     group_features.resize(feature_per_super_feature_,
//         vector<uint64_t>(super_feature_per_chunk_, 0));
//     uint32_t cur_group_id = 0;
//     uint32_t cur_group_offset = 0;
//
//    // 遍历所有特征点 (feature_per_chunk_ 个)
//     for (uint32_t i = 0; i < feature_per_chunk_; i++) {
//         uint8_t* sub_chunk_start = data + i * sub_chunk_size;
//         max_rabin_fp = 0;
//           // 设置当前子块范围
//         if (i == feature_per_chunk_ - 1) {
//             // this is the last sub-chunk
//             running_limit = last_chunk_size;
//         } else {
//             // this is not the last sub-chunk
//             running_limit = sub_chunk_size;
//         }
//
// // === 3. 处理子块 ===
//         for (uint32_t j = 0; j < running_limit; j++) {
//          // 滑动计算Rabin指纹
//             uint8_t* cur_data = sub_chunk_start + j;
//             tmp_rabin_fp = rabin_util_->SlideOneByte(ctx, *cur_data);
//              // 记录当前子块的最大指纹
//             if (tmp_rabin_fp > max_rabin_fp) {
//                 max_rabin_fp = tmp_rabin_fp;
//             }
//         }
//            // 存储当前子块的最大指纹
//         group_features[cur_group_id][cur_group_offset] = max_rabin_fp;
//         cur_group_offset++;
//          // 更新组索引
//         if (cur_group_offset == super_feature_per_chunk_) {
//             cur_group_id++;
//             cur_group_offset = 0;
//         }
//         rabin_util_->ResetCtx(ctx);
//     }
//     rabin_util_->ResetCtx(ctx);
//
//     // for (auto group : group_features) {
//     //     for (auto it: group) {
//     //         cout << it << " ";
//     //     }
//     //     cout << endl;
//     // }
//
//     // sort the features of each group     // === 4. 特征分组排序 ===
//     for (uint32_t i = 0; i < feature_per_super_feature_; i++) {
//         // max->min// 降序排序 (最大指纹优先)
//         sort(group_features[i].begin(), group_features[i].end(),
//             greater<uint64_t>());
//     }
//
//     // for (auto group : group_features) {
//     //     for (auto it: group) {
//     //         cout << it << " ";
//     //     }
//     //     cout << endl;
//     // }
//
//     // group features into super features    // === 5. 构建超级特征 ===
//     vector<vector<uint64_t>> super_features;
//     super_features.resize(super_feature_per_chunk_);
//      // 重组特征：每个超级特征包含来自不同组的特征
//     for (uint32_t i = 0; i < super_feature_per_chunk_; i++) {
//
//         for (uint32_t j = 0; j < feature_per_super_feature_; j++) {
//         // 取第j组的第i个特征
//             super_features[i].push_back(group_features[j][i]);
//         }
//     }
//
//     // for (auto feature : super_features) {
//     //     for (auto it : feature) {
//     //         cout << it << " ";
//     //     }
//     //     cout << endl;
//     // }
//
//     // compute the hash of the super features      // === 6. 计算最终特征哈希 ===
//     for (uint32_t i = 0; i < super_feature_per_chunk_; i++) {
//      // 对超级特征向量进行哈希
//         features[i] = XXHash64::hash((uint8_t*)super_features[i].data(),
//             sizeof(uint64_t) * feature_per_super_feature_, 0);
//     }

    return ;
}