/**
 * @file finesse.h
 * @author Zuoru YANG (zryang@cse.cuhk.edu.hk)
 * @brief define the interface of finesse
 * @version 0.1
 * @date 2022-06-09
 * 
 * @copyright Copyright (c) 2022
 * 
 */

 //---------------------------------
 //定义了一个名为 FinesseUtil 的工具类，它实现了 Finesse 特征提取算法，
 //用于在加密重复数据删除（EDR）系统中高效提取数据块的特征向量
 //特征提取：从数据块中提取固定大小的特征向量
//相似性表示：将数据块内容表示为紧凑的特征集合
//高效比较：支持快速计算数据块间的相似度
//-------------------------------------
#ifndef MY_CODEBASE_FINESSE_H
#define MY_CODEBASE_FINESSE_H

#include "../define.h"
#include "../configure.h"
#include "rabin_poly.h"
#include "xxhash64.h"

using namespace std;

extern Configure config;

class FinesseUtil {
    private:
        string my_name_ = "FinesseUtil";

        // configure  配置参数
        uint64_t super_feature_per_chunk_;// 每个数据块的超级特征数量
        uint64_t feature_per_chunk_;// 每个数据块的总特征数量
        uint64_t feature_per_super_feature_; // 每个超级特征包含的基础特征数量

        RabinFPUtil* rabin_util_;  // Rabin指纹计算工具

    public:
        /**
         * @brief Construct a new FinesseUtil object
         * 
         * @param super_feature_per_chunk super feature per chunk 
         * @param feature_per_chunk feature per chunk 
         * @param feature_per_super_feature feature per super feature
         */
        FinesseUtil(uint64_t super_feature_per_chunk, uint64_t feature_per_chunk,
            uint64_t feature_per_super_feature);

        /**
         * @brief Destroy the FinesseUtil object
         * 
         */
        ~FinesseUtil();

        /**
         * @brief compute the features from the chunk
         *
         * @param ctx the rabin ctx
         * @param data the chunk data
         * @param size the chunk size
         * @param features the chunk features
         */
        void ExtractFeature(RabinCtx_t& ctx, uint8_t* data, uint32_t size,
            uint64_t* features);
};

#endif