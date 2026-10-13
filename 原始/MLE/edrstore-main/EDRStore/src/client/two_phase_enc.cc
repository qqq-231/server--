/**
 * @file two_phase_enc.cc
 * @author Zuoru YANG (zryang@cse.cuhk.edu.hk)
 * @brief the impl of the two-phase 
 * @version 0.1
 * @date 2022-05-30
 * 
 * @copyright Copyright (c) 2022
 * 
 */

//-----------------------------------------
//实现了一个名为 TwoPhaseEnc 的类，用于执行两阶段加密（AES-256-CTR + AES-256-ECB）和解密操作
//-----------------------------------------------------------------




#include "../../include/client/two_phase_enc.h"

/**
 * @brief Construct a new Two Phase Enc object
 * 
 */
TwoPhaseEnc::TwoPhaseEnc() {
    crypto_util_ctr_ = new CryptoUtil(AES_256_CTR, SHA_256);
    crypto_util_ecb_ = new CryptoUtil(AES_256_ECB, SHA_256);
    memset(iv_, 0, CRYPTO_BLOCK_SIZE);
    cipher_ctx_ = EVP_CIPHER_CTX_new();
}

/**
 * @brief Destroy the Two Phase Enc object
 * 
 */
TwoPhaseEnc::~TwoPhaseEnc() {
    delete crypto_util_ctr_;
    delete crypto_util_ecb_;
    EVP_CIPHER_CTX_free(cipher_ctx_);
}

/**
 * @brief using two-phase to enc chunk
 * 
 * @param plain_chunk the plain chunk
 * @param size the chunk size
 * @param key the enc key
 * @param enc_chunk the encrypted chunk
 * @return uint32_t the encrypted chunk size
 */                //加密部分
uint32_t TwoPhaseEnc::TwoPhaseEncChunk(uint8_t* plain_chunk, uint32_t size, uint8_t* key,
    uint8_t* enc_chunk) {
    uint8_t tmp_cipher[ENC_MAX_CHUNK_SIZE];
    uint32_t cipher_size = 0;

    //使用 AES-256-CTR 模式加密原始数据。
    cipher_size = crypto_util_ctr_->EncryptWithKeyIV(cipher_ctx_, plain_chunk, size, key,
        iv_, tmp_cipher);
        // 将 CTR 加密结果用 AES-256-ECB 模式再次加密
    cipher_size = crypto_util_ecb_->EncryptWithKeyIV(cipher_ctx_, tmp_cipher, cipher_size, key,
        iv_, enc_chunk);
    return cipher_size;//最终密文大小
}

/**
 * @brief using two-phase to dec chunk
 * 
 * @param enc_chunk the encrypted chunk
 * @param size the chunk size
 * @param key the dec key
 * @param plain_chunk the plain chunk
 * @return uint32_t the plain chunk size
 */                   //解密
uint32_t TwoPhaseEnc::TwoPhaseDecChunk(uint8_t* enc_chunk, uint32_t size, uint8_t* key, 
    uint8_t* plain_chunk) {
    uint8_t tmp_cipher[ENC_MAX_CHUNK_SIZE];
    uint32_t cipher_size = 0;

    cipher_size = crypto_util_ecb_->DecryptWithKeyIV(cipher_ctx_, enc_chunk, size, key,
        iv_, tmp_cipher);
    cipher_size = crypto_util_ctr_->DecryptWithKeyIV(cipher_ctx_, tmp_cipher, cipher_size, key,
        iv_, plain_chunk);
    return cipher_size;
}