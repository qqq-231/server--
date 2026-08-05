/**
 * @file cipher_similar_thd.h
 * @author Zuoru YANG (zryang@cse.cuhk.edu.hk)
 * @brief define the thread to compute the ciphertext chunk features
 * @version 0.1
 * @date 2022-06-29
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#ifndef CIPHER_SIMILAR_THD_H
#define CIPHER_SIMILAR_THD_H

#include "../chunker/finesse_util.h"
#include "../chunker/n_transform.h"
#include "../chunker/rabin_poly.h"
#include "../message_queue/mq_factory.h"
#include "../data_structure.h"
#include "../chunker/odess_util.h"
#include "../chunker/gear_poly.h"
#include "../chunker/new_minhash.h"
#include "../chunker/odess_simple.h"
#include "../chunker/odess_start.h"
extern Configure config;

class CipherSimilarThd {
    private:
        string my_name_ = "CipherSimilarThd";
        string feature_2=config.Getfeature_2();

GearCtx_t gear_ctx_;  // gear指纹计算上下文
    GearHashUtil* gear_util_;  // gear指纹计算工具

     OdessUtil* odess_util_;
    // GearCtx_t gear_ctx_;  // gear指纹计算上下文
    // GearHashUtil* gear_util_;  // gear指纹计算工具


OdessStart* odess_start_;


    //=================OdessSimple
     OdessSimple* odess_simple_;
    // GearCtx_t gear_ctx_;  // gear指纹计算上下文
    // GearHashUtil* gear_util_;  // gear指纹计算工具

         FinesseUtil* finesse_util_;
         RabinCtx_t rabin_ctx_;
         RabinFPUtil* rabin_util_;




    New_MinHash* new_minhash_;




    //================= n_transform
     NTransform* n_transform;
    // GearCtx_t gear_ctx_;  // gear指纹计算上下文
    // GearHashUtil* gear_util_;  // gear指纹计算工具


    public:
#ifdef EDR_BREAKDOWN
        struct timeval _cipher_feature_stime;
        struct timeval _cipher_feature_etime;
        double _total_cipher_feature_time = 0;
        uint64_t _total_cipher_feature_size = 0;
#endif
        /**
         * @brief Construct a new Cipher Similar Thd object
         * 
         */
        CipherSimilarThd();

        /**
         * @brief Destroy the Cipher Similar Thd object
         * 
         */
        ~CipherSimilarThd();

        /**
         * @brief the main thread
         * 
         * @param input_MQ the input MQ
         */
        void Run(AbsMQ<EncFeatureChunk_t>* input_MQ, AbsMQ<EncFeatureChunk_t>* output_MQ);
};

#endif