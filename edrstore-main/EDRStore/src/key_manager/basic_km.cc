/**
 * @file basicKM.cc
 * @author Zuoru YANG (zryang@cse.cuhk.edu.hk)
 * @brief 
 * @version 0.1
 * @date 2022-04-23
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include "../../include/key_manager/basic_km.h"

#include <openssl/crypto.h>
#include <sys/mman.h>

/**
 * @brief Construct a new Basic KM object
 * 
 * @param km_channel the key generation channel
 * @param feature_2_key_index the feature index
 */
BasicKM::BasicKM(SSLConnection* km_channel, 
    AbsDatabase* feature_2_key_index,
    const edrstore::security::KMMasterKey& km_master) {
    km_channel_ = km_channel;
    feature_2_key_index_ = feature_2_key_index;

    send_chunk_batch_size_ = config.GetSendChunkBatchSize();
    static_assert(CHUNK_HASH_SIZE ==
        edrstore::security::KMMasterKey::kSecretSize);
    memcpy(global_secret_, km_master.GlobalSecret(), CHUNK_HASH_SIZE);
    memcpy(global_padding_secret_, km_master.PaddingSecret(), CHUNK_HASH_SIZE);
    if (mlock(global_secret_, sizeof(global_secret_)) == 0) {
        global_secret_locked_ = true;
    } else {
        tool::Logging(my_name_.c_str(),
            "SECURITY WARNING: cannot lock credential buffer A.\n");
    }
    if (mlock(global_padding_secret_, sizeof(global_padding_secret_)) == 0) {
        global_padding_secret_locked_ = true;
    } else {
        tool::Logging(my_name_.c_str(),
            "SECURITY WARNING: cannot lock credential buffer B.\n");
    }
    crypto_util_ = new CryptoUtil(CIPHER_TYPE, HASH_TYPE);

    similar_policy_ = new SimilarPolicy();
}

/**
 * @brief Destroy the Basic KM object
 * 
 */
BasicKM::~BasicKM() {
    OPENSSL_cleanse(global_secret_, sizeof(global_secret_));
    OPENSSL_cleanse(global_padding_secret_, sizeof(global_padding_secret_));
    if (global_secret_locked_) {
        munlock(global_secret_, sizeof(global_secret_));
    }
    if (global_padding_secret_locked_) {
        munlock(global_padding_secret_, sizeof(global_padding_secret_));
    }
    delete crypto_util_;
    delete similar_policy_;
    fprintf(stderr, "========BasicKM Info========\n");
    fprintf(stderr, "key gen num: %lu\n", _total_key_gen_num);
    fprintf(stderr, "similar chunk num: %lu\n", _total_similar_chunk_num);
    fprintf(stderr, "total key index size (B): %lu\n",
        (_total_key_gen_num - _total_similar_chunk_num) *
            SUPER_FEATURE_PER_CHUNK * (sizeof(uint64_t) + CHUNK_HASH_SIZE));
    fprintf(stderr, "============================\n");
}

/**
 * @brief the main process of the client
 * 
 * @param key_client_ssl the client ssl
 */
void BasicKM::Run(SSL* key_client_ssl) {
    tool::Logging(my_name_.c_str(), "the main thread is running.\n");
    uint32_t recv_size = 0;
    string client_ip;
    EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();

    // the recv buffer
    SendMsgBuffer_t recv_req_buf;
    recv_req_buf.send_buf = (uint8_t*) malloc(sizeof(NetworkHead_t) + 
        send_chunk_batch_size_ * sizeof(KeyGenReq_t));
    recv_req_buf.header = (NetworkHead_t*) recv_req_buf.send_buf;  
    recv_req_buf.data_buf = recv_req_buf.send_buf + sizeof(NetworkHead_t);
    uint32_t client_id = 0;

    // the send buffer
    SendMsgBuffer_t send_key_buf;
    send_key_buf.send_buf = (uint8_t*) malloc(sizeof(NetworkHead_t) +
        send_chunk_batch_size_ * sizeof(KeyGenRet_t));
    send_key_buf.header = (NetworkHead_t*) send_key_buf.send_buf;
    send_key_buf.data_buf = send_key_buf.send_buf + sizeof(NetworkHead_t);

    struct timeval stime;
    struct timeval etime;
    struct timeval s_total_time;
    struct timeval e_total_time;
    double total_running_time = 0;
    double total_proc_time = 0;

    gettimeofday(&s_total_time, NULL);
    // -------- main process ---------

    uint8_t tmp_hash_buf[CHUNK_HASH_SIZE * 2] = {0};
    memcpy(tmp_hash_buf, global_secret_, CHUNK_HASH_SIZE);

    uint8_t tmp_feature_buf[CHUNK_HASH_SIZE + sizeof(uint64_t) * 3] = {0};
    memcpy(tmp_feature_buf, global_secret_, CHUNK_HASH_SIZE);

    // uint8_t tmp_padding_hash_buf[CHUNK_HASH_SIZE * 2] = {0};
    // memcpy(tmp_padding_hash_buf, global_padding_secret_,
    //     CHUNK_HASH_SIZE);

    unordered_map<string, uint32_t> feature_freq_map;
    while (true) {
        // recv data
        if (!km_channel_->ReceiveData(key_client_ssl, recv_req_buf.send_buf,
            recv_size)) {
            tool::Logging(my_name_.c_str(), "client closed socket connect, thread exit now.\n");
            km_channel_->GetClientIp(client_ip, key_client_ssl);
            km_channel_->ClearAcceptedClientSd(key_client_ssl);
            client_id = recv_req_buf.header->client_id;
            break;
        } else {
            gettimeofday(&stime, NULL);
            if (recv_req_buf.header->msg_type != CLIENT_KEY_GEN) {
                tool::Logging(my_name_.c_str(), "wrong key gen req type.\n");
                exit(EXIT_FAILURE);
            }

            // perform simple key generation
            uint32_t recv_fp_num = recv_req_buf.header->cur_item_num;
            string base_key;
            base_key.resize(CHUNK_HASH_SIZE, 0);
            
            KeyGenReq_t* cur_key_gen_req = (KeyGenReq_t*) recv_req_buf.data_buf;
            KeyGenRet_t* cur_key_gen_ret = (KeyGenRet_t*) send_key_buf.data_buf;
            ChunkInfo_t tmp_info;
            for (size_t i = 0; i < recv_fp_num; i++) {
                // memcpy(tmp_padding_hash_buf + CHUNK_HASH_SIZE, cur_key_gen_req->fp,
                //     CHUNK_HASH_SIZE);
                // crypto_util_->GenerateHash(md_ctx, tmp_padding_hash_buf, CHUNK_HASH_SIZE * 2,
                //     cur_key_gen_ret->key);
                // // DEBUG: test with similar-aware padding
                // cur_key_gen_ret->seed = this->ConvertFp2Val(cur_key_gen_ret->key,
                //     CHUNK_HASH_SIZE);

                memcpy(tmp_info.features, cur_key_gen_req->features,
                    sizeof(uint64_t) * SUPER_FEATURE_PER_CHUNK); 
                similar_policy_->FindBaseChunk(feature_2_key_index_, &tmp_info);

                switch (tmp_info.stat) {
                    case SIMILAR_CHUNK: {
                        // similar chunk  // 直接使用相似块的密钥
                        memcpy(cur_key_gen_ret->key_seed, tmp_info.addr.base_fp,
                            CHUNK_HASH_SIZE);
                        _total_similar_chunk_num++;
                        break;
                    }
                    case NON_SIMILAR_CHUNK: {
                        // not a similar chunk
                        // memcpy(tmp_hash_buf + CHUNK_HASH_SIZE, cur_key_gen_req->fp,
                        //     CHUNK_HASH_SIZE);
                        // crypto_util_->GenerateHash(md_ctx, tmp_hash_buf, CHUNK_HASH_SIZE * 2,
                        //     cur_key_gen_ret->key);

                        
                        // generate new key here    // 组合全局秘密和特征值生成新密钥
                        // 密钥种子 = Hash(全局秘密+特征1+特征2+特征3)   ‘+'是连接
                        memcpy(tmp_feature_buf + CHUNK_HASH_SIZE, &tmp_info.features[0], 
                            sizeof(uint64_t));
                        memcpy(tmp_feature_buf + CHUNK_HASH_SIZE + sizeof(uint64_t), 
                            &tmp_info.features[1], sizeof(uint64_t));
                        memcpy(tmp_feature_buf + CHUNK_HASH_SIZE + sizeof(uint64_t) * 2, 
                            &tmp_info.features[2], sizeof(uint64_t));

                            // 使用加密哈希生成密钥
                        crypto_util_->GenerateHash(md_ctx, tmp_feature_buf, CHUNK_HASH_SIZE + 
                            sizeof(uint64_t) * 3, cur_key_gen_ret->key_seed);                   

                        // update the index  // 更新索引：将新特征和密钥关联
                        similar_policy_->UpdateFeatureIndex(feature_2_key_index_,
                            tmp_info.features, cur_key_gen_ret->key_seed);
                        break;
                    }
                    default: {
                        tool::Logging(my_name_.c_str(), "wrong key type.\n");
                        exit(EXIT_FAILURE);
                    }
                }

                // DEBUG: test with similar-aware padding
                // cur_key_gen_ret->seed = this->ConvertFp2Val(cur_key_gen_ret->key,
                    // CHUNK_HASH_SIZE);

                cur_key_gen_req++;
                cur_key_gen_ret++;
            }

            gettimeofday(&etime, NULL);
            total_proc_time += tool::GetTimeDiff(stime, etime);

            // send the key gen result back to the client  设置响应消息头
            //size：响应数据的总字节数（密钥数量 × 每个密钥结构大小）
            // cur_item_num：响应的密钥数量（与请求中的特征数相同）
            //msg_type：消息类型，标识这是密钥生成响应

            send_key_buf.header->size = recv_fp_num * sizeof(KeyGenRet_t);
            send_key_buf.header->cur_item_num = recv_fp_num;
            send_key_buf.header->msg_type = KEY_MANAGER_KEY_GEN_REPLY;

            //通过SSL连接发送密钥生成结果给客户端
            if (!km_channel_->SendData(key_client_ssl, send_key_buf.send_buf, 
                sizeof(NetworkHead_t) + send_key_buf.header->size)) {
                tool::Logging(my_name_.c_str(), "send the key gen errors.\n");
                exit(EXIT_FAILURE);
            }
            _total_key_gen_num += recv_fp_num;
        }
    }

    gettimeofday(&e_total_time, NULL);
    total_running_time += tool::GetTimeDiff(s_total_time, e_total_time);

    EVP_MD_CTX_free(md_ctx);
    free(recv_req_buf.send_buf);
    free(send_key_buf.send_buf);
    tool::Logging(my_name_.c_str(), "thread exits for %s, ID: %u, total process time: %lf\n", 
        client_ip.c_str(), client_id, total_proc_time);

    return ;
}

/**
 * @brief convert the fp to value
 * 
 * @param fp chunk fp
 * @param size chunk size
 * @return uint64_t fp value
 */
//将二进制指纹（fingerprint）转换为一个64位整数值
uint64_t BasicKM::ConvertFp2Val(uint8_t* fp, uint32_t size) {
    uint64_t hash_val = 0;
    for (size_t i = 0; i < CHUNK_HASH_SIZE; i++) {
        hash_val += fp[i];
    }
    return hash_val;
}
