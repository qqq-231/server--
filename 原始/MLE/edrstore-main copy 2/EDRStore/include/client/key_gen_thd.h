/**
 * @file key_gen_thd.h
 * @author Zuoru YANG (zryang@cse.cuhk.edu.hk)
 * @brief define the interface of KeyGenThd
 * @version 0.1
 * @date 2022-06-11
 * 
 * @copyright Copyright (c) 2022
 * 
 */

 //-------------------------------------------------
 //定义了一个名为 KeyGenThd 的类，它是 EDR（端点检测与响应）系统中用于密钥生成和加密处理的工作线程。
 //主要功能是批量处理特征数据块，执行两阶段加密，并与密钥管理器（Key Manager）通信获取加密密钥
 //--------------------------------------------------


#ifndef EDRSTORE_KEY_GEN_THD_H
#define EDRSTORE_KEY_GEN_THD_H

#include "../message_queue/mq_factory.h"
#include "../data_structure.h"
#include "../network/ssl_conn.h"
#include "../configure.h"
#include "../client/two_phase_enc.h"
#include "../database/db_factory.h"

extern Configure config;

class KeyGenThd {
    private:
        string my_name_ = "KeyGenThd";

        // config
        uint64_t send_chunk_batch_size_ = 0;
        SendMsgBuffer_t send_buf_;
        SendMsgBuffer_t recv_buf_;

        // to batch the plaintext chunk
        vector<EncFeatureChunk_t> chunk_buf_;

        // for communication
        SSLConnection* km_channel_;
        pair<int, SSL*> km_conn_record_;
        SSL* km_ssl_;

        // two-phase encryption 
        TwoPhaseEnc* two_phase_enc_;

        // for crypto
        CryptoUtil* crypto_util_;
        EVP_MD_CTX* md_ctx;

        /**
         * @brief add the chunk to the batch plaintext chunk buffer
         * 
         * @param input_chunk the input chunk
         * @param output_MQ the output MQ
         */
        void AddChunkToBuf(EncFeatureChunk_t& input_chunk,
            AbsMQ<EncFeatureChunk_t>* output_MQ);

        /**
         * @brief process a batch of chunks
         * 
         * @param output_MQ the output MQ
         */
        void ProcessBatch(AbsMQ<EncFeatureChunk_t>* output_MQ);
    
    public:
        uint64_t _performance_encryption_time_ns = 0;
        uint64_t _performance_key_generation_time_ns = 0;
#ifdef EDR_BREAKDOWN
        struct timeval _two_enc_stime;
        struct timeval _two_enc_etime;
        double _total_two_enc_time = 0;
        uint64_t _total_two_enc_size = 0;

        struct timeval _key_gen_stime;
        struct timeval _key_gen_etime;
        double _total_key_gen_time = 0;
#endif
        /**
         * @brief Construct a new KeyGenThd object
         * 
         * @param km_channel the key manager connection channel
         * @param km_conn_record the key manager connection channel
         */
        KeyGenThd(SSLConnection* km_channel, pair<int, SSL*> km_conn_record);

        /**
         * @brief Destroy the KeyGenThd object
         * 
         */
        ~KeyGenThd();

        /**
         * @brief the main thread
         * 
         * @param input_MQ the input MQ
         * @param output_MQ the output MQ
         */
        void Run(AbsMQ<FeatureChunk_t>* input_MQ,
            AbsMQ<EncFeatureChunk_t>* output_MQ);
        
        uint64_t ConvertFp2Val(uint8_t* fp, uint32_t size);
};

#endif
