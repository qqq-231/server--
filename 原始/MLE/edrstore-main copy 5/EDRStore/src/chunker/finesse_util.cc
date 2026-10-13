/**
 * @file finesse_util.cc
 * @author Zuoru YANG (zryang@cse.cuhk.edu.hk)
 * @brief 
 * @version 0.1
 * @date 2022-06-09
 * 
 * @copyright Copyright (c) 2022
 * 
 */

 //-----------------------------
 //实现了 Finesse 特征提取算法的实现
 //--------------------------------


#include "../../include/chunker/finesse_util.h"

/**
 * @brief Construct a new FinesseUtil object
 * 
 * @param super_feature_per_chunk super feature per chunk 
 * @param feature_per_chunk feature per chunk 
 * @param feature_per_super_feature feature per super feature
 */
FinesseUtil::FinesseUtil(uint64_t super_feature_per_chunk, uint64_t feature_per_chunk,
    uint64_t feature_per_super_feature) {
     // 设置特征参数
    super_feature_per_chunk_ = super_feature_per_chunk;
    feature_per_chunk_ = feature_per_chunk;
    feature_per_super_feature_ = feature_per_super_feature;

   // 初始化 Rabin 指纹工具（使用配置的滑动窗口大小）（窗口大小=48字节） config，json里设置
    rabin_util_ = new RabinFPUtil(config.GetSimilarSlidingWinSize());
    //参数关系：feature_per_chunk = super_feature_per_chunk * feature_per_super_feature
}

/**
 * @brief Destroy the FinesseUtil object
 * 
 */
FinesseUtil::~FinesseUtil() {
    delete rabin_util_;
}

/**
 * @brief compute the features from the chunk
 *
 * @param ctx the rabin ctx
 * @param data the chunk data
 * @param size the chunk size
 * @param features the chunk features
 */
void FinesseUtil::ExtractFeature(RabinCtx_t& ctx, uint8_t* data, uint32_t size,
    uint64_t* features) {
     // === 1. 初始化 ===
    rabin_util_->ResetCtx(ctx);
    uint64_t sub_chunk_size;
    uint32_t last_chunk_size;
    uint32_t running_limit;

      // 计算子块大小
    sub_chunk_size = size / feature_per_chunk_;
    last_chunk_size = sub_chunk_size + (size % feature_per_chunk_);

 // === 2. 数据块分组处理 ===
    uint64_t tmp_rabin_fp = 0;
    uint64_t max_rabin_fp = 0;
    vector<vector<uint64_t>> group_features;
    group_features.resize(feature_per_super_feature_,
        vector<uint64_t>(super_feature_per_chunk_, 0));
    uint32_t cur_group_id = 0;
    uint32_t cur_group_offset = 0;

   // 遍历所有特征点 (feature_per_chunk_ 个)
    for (uint32_t i = 0; i < feature_per_chunk_; i++) {
        uint8_t* sub_chunk_start = data + i * sub_chunk_size;
        max_rabin_fp = 0;
          // 设置当前子块范围
        if (i == feature_per_chunk_ - 1) {
            // this is the last sub-chunk
            running_limit = last_chunk_size;
        } else {
            // this is not the last sub-chunk
            running_limit = sub_chunk_size;
        }

// === 3. 处理子块 ===
        for (uint32_t j = 0; j < running_limit; j++) {
         // 滑动计算Rabin指纹
            uint8_t* cur_data = sub_chunk_start + j;
            tmp_rabin_fp = rabin_util_->SlideOneByte(ctx, *cur_data);
             // 记录当前子块的最大指纹
            if (tmp_rabin_fp > max_rabin_fp) {
                max_rabin_fp = tmp_rabin_fp;
            }
        }
           // 存储当前子块的最大指纹
        group_features[cur_group_id][cur_group_offset] = max_rabin_fp;
        cur_group_offset++;
         // 更新组索引
        if (cur_group_offset == super_feature_per_chunk_) {
            cur_group_id++;
            cur_group_offset = 0;
        }
        rabin_util_->ResetCtx(ctx);
    }
    rabin_util_->ResetCtx(ctx);

    // for (auto group : group_features) {
    //     for (auto it: group) {
    //         cout << it << " ";
    //     }
    //     cout << endl;
    // }

    // sort the features of each group     // === 4. 特征分组排序 ===
    for (uint32_t i = 0; i < feature_per_super_feature_; i++) {
        // max->min// 降序排序 (最大指纹优先)
        sort(group_features[i].begin(), group_features[i].end(),
            greater<uint64_t>());
    }

    // for (auto group : group_features) {
    //     for (auto it: group) {
    //         cout << it << " ";
    //     }
    //     cout << endl;
    // }

    // group features into super features    // === 5. 构建超级特征 ===
    vector<vector<uint64_t>> super_features;
    super_features.resize(super_feature_per_chunk_);
     // 重组特征：每个超级特征包含来自不同组的特征
    for (uint32_t i = 0; i < super_feature_per_chunk_; i++) {

        for (uint32_t j = 0; j < feature_per_super_feature_; j++) {
        // 取第j组的第i个特征
            super_features[i].push_back(group_features[j][i]);
        }
    }

    // for (auto feature : super_features) {
    //     for (auto it : feature) {
    //         cout << it << " ";
    //     }
    //     cout << endl;
    // }

    // compute the hash of the super features      // === 6. 计算最终特征哈希 ===
    for (uint32_t i = 0; i < super_feature_per_chunk_; i++) {
     // 对超级特征向量进行哈希
        features[i] = XXHash64::hash((uint8_t*)super_features[i].data(),
            sizeof(uint64_t) * feature_per_super_feature_, 0);
    }
     // tool::Logging(my_name_.c_str(), "use finesse 算法\n");
    return ;
}