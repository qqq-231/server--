/**
 * @file plain_similar_thd.h
 * @author Zuoru YANG (zryang@cse.cuhk.edu.hk)
 * @brief  define the thread to compute the plaintext chunk features
 * @version 0.1
 * @date 2022-06-10
 *
 * @copyright Copyright (c) 2022
 *
 */

 //-------------------------------------
 //定义了一个名为 PlainSimilarThd 的类，它是安全存储系统中负责 明文相似性检测 的核心组件
 //明文特征提取：在数据加密前分析数据块的相似性特征
//相似性检测：识别内容相似的数据块
//特征生成：为每个数据块生成特征向量
//为后续加密优化准备：提供相似性信息以支持更高效的加密和去重
 //------------------------------------

#ifndef PLAIN_SIMILAR_THD_H
#define PLAIN_SIMILAR_THD_H

#include "../chunker/finesse_util.h"
#include "../chunker/rabin_poly.h"
#include "../chunker/odess_simple.h"
#include "../chunker/new_minhash.h"
#include "../chunker/n_transform.h"
#include "../chunker/n_transform_gear.h"
#include "../chunker/odess_util.h"
#include "../chunker/gear_poly.h"
#include "../message_queue/mq_factory.h"
#include "../data_structure.h"
#include "../configure.h"


class OdessStart;
extern Configure config;

class PlainSimilarThd {
    private:
        string my_name_ = "PlainSimilarThd";
        string feature=config.Getfeature_1();


        //
        // //===========Finesse=======
        FinesseUtil* finesse_util_ = nullptr;// Finesse特征提取工具
        RabinCtx_t rabin_ctx_;  // Rabin指纹计算上下文
        RabinFPUtil* rabin_util_ = nullptr;  // Rabin指纹计算工具


   // ========= OdessUtil
     OdessUtil* odess_util_ = nullptr;
     GearCtx_t gear_ctx_;  // gear指纹计算上下文
     GearHashUtil* gear_util_ = nullptr;  // gear指纹计算工具



    OdessStart* odess_start_ = nullptr;

 //=================OdessSimple
     OdessSimple* odess_simple_ = nullptr;
     // GearCtx_t gear_ctx_;  // gear指纹计算上下文
     // GearHashUtil* gear_util_;  // gear指纹计算工具


 //================= n_transform (Rabin-based)
      NTransform* n_transform = nullptr;


     //
     New_MinHash* new_minhash_ = nullptr;
     // GearCtx_t gear_ctx_;  // gear指纹计算上下文
     // GearHashUtil* gear_util_;  // gear指纹计算工具


 //================= n_transform_gear (Gear-based)
     NTransform_gear* n_transform_gear = nullptr;


    public:
        uint64_t _performance_feature_time_ns = 0;
#ifdef EDR_BREAKDOWN
        struct timeval _plain_feature_stime;// 特征提取开始时间
        struct timeval _plain_feature_etime;// 特征提取结束时间
        double _total_plain_feature_time = 0;// 总特征提取耗时
        uint64_t _total_plain_feature_size = 0;// 总处理数据量
#endif
        /**
         * @brief Construct a new PlainSimilarThd object
         *
         */
        PlainSimilarThd();

        /**
         * @brief Destroy the PlainSimilarThd object
         *
         */
        ~PlainSimilarThd();

        /**
         * @brief the main thread
         *
         * @param input_MQ the input MQ
         * @param output_MQ the output MQ
         */
        void Run(AbsMQ<Chunk_t>* input_MQ, AbsMQ<FeatureChunk_t>* output_MQ);
};

#endif
