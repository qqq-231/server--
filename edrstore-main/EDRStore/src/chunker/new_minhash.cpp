//
// Created by zhao2 on 2025/12/5.
//

#include "../../include/chunker/new_minhash.h"
#include "../../include/MA.h"

#include <openssl/sha.h>
#include "../../include/chunker/gear_poly.h"
#include "../../include/const_var.h"

static const uint64_t MINHASH_SEEDS[32] = {
    0x1f2d3e4b5a678901ULL, 0x9abc7def01234567ULL, 0x89ab7cde9f012345ULL, 0x456789ab7cde9f01ULL,
    0x23456789abcdef01ULL, 0xfedcba9876543210ULL, 0x0123456789abcdefULL, 0xabcdef0123456789ULL,
    0x9876543210fedcbaULL, 0x89abcdef01234567ULL, 0x456789abcdef0123ULL, 0x0123456789abcdefULL,
    0xdeadbeefcafebabeULL, 0x8badf00ddeadc0deULL, 0xcafebabedeadbeefULL, 0x1337c0de1337beefULL,
    0x1928374655647382ULL, 0xaabbccddeeff0011ULL, 0x2233445566778899ULL, 0x9988776655443322ULL,
    0x1122334455667788ULL, 0x8877665544332211ULL, 0x0102030405060708ULL, 0x0807060504030201ULL,
    0x13579bdf2468ace0ULL, 0x2468ace013579bdfULL, 0x369cf1e2d4b8a750ULL, 0x5a5a5a5a5a5a5a5aULL,
    0x1234567890abcdefULL, 0xfedcba0987654321ULL, 0xcafebabebadf00d8ULL, 0x8badf00dcafebabeULL
};

//uint64_t minhash_sig[32]={0};



/**
 * @brief Construct a new FinesseUtil object
 *
 * @param super_feature_per_chunk super feature per chunk
 * @param feature_per_chunk feature per chunk
 * @param feature_per_super_feature feature per super feature
 */


New_MinHash::New_MinHash(uint64_t super_feature_per_chunk, uint64_t feature_per_chunk,
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
New_MinHash::~New_MinHash() {
    delete gear_util_;
}

/**
 * @brief compute the features from the chunk
 *
 * @param ctx the rabin ctx
 * @param data the chunk data
 * @param size the chunk size
 * @param features the chunk features
 */
void New_MinHash::ExtractFeature(GearCtx_t& ctx, uint8_t* data, uint32_t size,
    uint64_t* features)
{
    //原始数据块的起始地址   原始数据块的大小（字节数）  输出特征数组

    std::vector<uint32_t> sub_features(super_feature_per_chunk_, 0);


    const uint32_t WIN = gear_util_->GetWindowSize();

 //tool::Logging("New_MinHash", "Config: win_size=%u, mask=0x%llx", WIN);

    if (size < WIN) {
        // 小 chunk 直接哈希

        unsigned char sha256_hash[32];
        SHA256((const unsigned char*)data,size, sha256_hash);

        // 取前 8 字节作为 64bit 哈希值（足够随机）
//        uint64_t h = *(uint64_t*)sha256_hash;
        __uint128_t h = *(__uint128_t*)sha256_hash;
        for (uint32_t j = 0; j < super_feature_per_chunk_; ++j) {
            features[j] =XXHash64::hash(&h, 16, MINHASH_SEEDS[j]);
        }
        return;
    }




    // === 1. 初始化 ===
    gear_util_->ResetCtx(ctx);
    //gear_util_->UpdateCtx(ctx, data, WIN);

    //std::fill(features, features + super_feature_per_chunk_, UINT64_MAX);

    std::fill(features, features + super_feature_per_chunk_, UINT64_MAX);
    gear_util_->ResetCtx(ctx);

    int last_boundary = 0;

    // 直接从第 0 字节开始滑！（和 Odess 一模一样）
    for (uint32_t i = 0; i < size; ++i) {
        uint64_t fp = gear_util_->SlideOneByte(ctx, data[i]);

        // 注意：要跳过前 WIN-1 个字节，因为窗口还没满
        if (i >= WIN - 1 && gear_util_->IsChunkBoundary(fp)) {
            uint32_t sub_len = i - last_boundary + 1;

            unsigned char hash[32];
            SHA256(data + last_boundary, sub_len, hash);
            uint64_t h64 = *(uint64_t*)hash;

            for (uint32_t j = 0; j < super_feature_per_chunk_; ++j) {
                uint64_t proj = XXHash64::hash(&h64, 8, MINHASH_SEEDS[j]);
                if (proj < features[j]) {
                    features[j] = proj;
                }
            }
            last_boundary = i + 1;
        }
    }

    // 最后一个子块
    if (last_boundary < (int)size) {
        unsigned char hash[32];
        SHA256(data + last_boundary, size - last_boundary, hash);
        uint64_t h64 = *(uint64_t*)hash;
        for (uint32_t j = 0; j < super_feature_per_chunk_; ++j) {
            uint64_t proj = XXHash64::hash(&h64, 8, MINHASH_SEEDS[j]);
            if (proj < features[j]) features[j] = proj;
        }
    }

  //   int chunk_length = size;
  //  // int small_chunk_length = 0;
  //   uint64_t finger_print = 0;
  // //  int last_pos = 0;
  //  // int last_boundary = 0;
  //   // calculate sub features.
  //   for (int i = WIN; i < chunk_length; i++)
  //   {
  //      // uint32_t len = i - last_pos + 1;
  //       uint8_t* cur_data =  data + i ;
  //       finger_print = gear_util_->SlideOneByte(ctx, *cur_data);
  //
  //
  //       // finger_print = (finger_print << 1) + GEAR_TABLE[content[i]];
  //       //----=====---
  //       if (gear_util_->IsChunkBoundary(finger_print))
  //       {
  //
  //
  //
  //           unsigned char sha256_hash[32];
  //           SHA256((const unsigned char*)cur_data-WIN, WIN, sha256_hash);
  //
  //           // 取前 8 字节作为 64bit 哈希值（足够随机）
  //           uint64_t h = *(uint64_t*)sha256_hash;
  //
  //           //
  //           // small_chunk_length=i-small_chunk_length;
  //           //
  //
  //           // 第二步：对 32 个固定种子做 MinHash//super_feature_per_chunk_个
  //           for (int i = 0; i < super_feature_per_chunk_; ++i) {
  //               // uint64_t projected = XXH64(&h, 8, MINHASH_SEEDS[i]);
  //               uint64_t projected = XXHash64::hash(&h, sizeof(h), MINHASH_SEEDS[i]);
  //               if (projected < features[i]) {
  //                  features[i] = projected;
  //               }
  //           }
  //       }
  //   }
  //
  //   // for (int i = 0; i < super_feature_per_chunk_; i++) {  // 注意这里应该是 super_feature_per_chunk_
  //   //     features[i] =  minhash_sig[i];
  //   // }
  //


    tool::Logging(my_name_.c_str(), "初始化  use new_minhash.\n");

    gear_util_->ResetCtx(ctx);




}



