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
// 使用现有32字节块密钥执行AES-128-XTS加密和解密。
// XTS将该密钥解释为两个独立的128位AES密钥。
//-----------------------------------------------------------------




#include "../../include/client/two_phase_enc.h"

/**
 * @brief Construct a new Two Phase Enc object
 * 
 */
TwoPhaseEnc::TwoPhaseEnc() {
    crypto_util_ctr_ = new CryptoUtil(AES_256_CTR, SHA_256);
    memset(iv_, 0, CRYPTO_BLOCK_SIZE);
    cipher_ctx_ = EVP_CIPHER_CTX_new();
}

/**
 * @brief Destroy the Two Phase Enc object
 * 
 */
TwoPhaseEnc::~TwoPhaseEnc() {
    delete crypto_util_ctr_;
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
    return this->XTSChunk(plain_chunk, size, key, enc_chunk, true);
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
    return this->XTSChunk(enc_chunk, size, key, plain_chunk, false);
}

bool TwoPhaseEnc::BuildXTSTweak(const uint8_t* key, uint8_t* tweak) {
    static const uint8_t tweak_domain[] = "EDRStore-AES-XTS-tweak";
    uint8_t tweak_material[sizeof(tweak_domain) - 1 + CHUNK_HASH_SIZE];
    uint8_t tweak_hash[CHUNK_HASH_SIZE];

    memcpy(tweak_material, tweak_domain, sizeof(tweak_domain) - 1);
    memcpy(tweak_material + sizeof(tweak_domain) - 1, key, CHUNK_HASH_SIZE);

    unsigned int hash_size = 0;
    if (EVP_Digest(tweak_material, sizeof(tweak_material), tweak_hash,
        &hash_size, EVP_sha256(), NULL) != 1 ||
        hash_size < CRYPTO_BLOCK_SIZE) {
        return false;
    }
    memcpy(tweak, tweak_hash, CRYPTO_BLOCK_SIZE);
    return true;
}

uint32_t TwoPhaseEnc::XTSChunk(uint8_t* input, uint32_t size, uint8_t* key,
    uint8_t* output, bool encrypt) {
    if (input == NULL || key == NULL || output == NULL || size == 0 ||
        cipher_ctx_ == NULL) {
        tool::Logging(my_name_.c_str(), "invalid AES-XTS input.\n");
        return 0;
    }

    // OpenSSL XTS requires at least one complete AES block. Preserve the
    // existing length and format for a possible tiny final chunk by using the
    // original CTR primitive only for this otherwise unsupported edge case.
    if (size < CRYPTO_BLOCK_SIZE) {
        if (encrypt) {
            return crypto_util_ctr_->EncryptWithKeyIV(cipher_ctx_, input,
                size, key, iv_, output);
        }
        return crypto_util_ctr_->DecryptWithKeyIV(cipher_ctx_, input,
            size, key, iv_, output);
    }

    uint8_t tweak[CRYPTO_BLOCK_SIZE];
    if (!this->BuildXTSTweak(key, tweak)) {
        tool::Logging(my_name_.c_str(), "cannot derive AES-XTS tweak.\n");
        return 0;
    }

    EVP_CIPHER_CTX_reset(cipher_ctx_);
    int output_size = 0;
    int final_size = 0;
    int ret = encrypt ?
        EVP_EncryptInit_ex(cipher_ctx_, EVP_aes_128_xts(), NULL, key,
            tweak) :
        EVP_DecryptInit_ex(cipher_ctx_, EVP_aes_128_xts(), NULL, key,
            tweak);
    if (ret != 1) {
        tool::Logging(my_name_.c_str(), "cannot initialize AES-XTS.\n");
        EVP_CIPHER_CTX_reset(cipher_ctx_);
        return 0;
    }

    if (EVP_CIPHER_CTX_set_padding(cipher_ctx_, 0) != 1) {
        tool::Logging(my_name_.c_str(), "cannot disable AES-XTS padding.\n");
        EVP_CIPHER_CTX_reset(cipher_ctx_);
        return 0;
    }

    ret = encrypt ?
        EVP_EncryptUpdate(cipher_ctx_, output, &output_size, input, size) :
        EVP_DecryptUpdate(cipher_ctx_, output, &output_size, input, size);
    if (ret != 1) {
        tool::Logging(my_name_.c_str(), "AES-XTS update failed.\n");
        EVP_CIPHER_CTX_reset(cipher_ctx_);
        return 0;
    }

    ret = encrypt ?
        EVP_EncryptFinal_ex(cipher_ctx_, output + output_size, &final_size) :
        EVP_DecryptFinal_ex(cipher_ctx_, output + output_size, &final_size);
    EVP_CIPHER_CTX_reset(cipher_ctx_);
    if (ret != 1) {
        tool::Logging(my_name_.c_str(), "AES-XTS finalization failed.\n");
        return 0;
    }
    return output_size + final_size;
}
