
//------------------------------------------
//定义了一个安全存储系统（EDRStore）的核心常量、枚举和配置参数
//----------------------------------------
/**
 * @file const_var.h
 * @author Zuoru YANG (zryang@cse.cuhk.edu.hk)
 * @brief define the const variables
 * @version 0.1
 * @date 2022-02-05
 * 
 * @copyright Copyright (c) 2022
 * 
 */
#ifndef EDRSTORE_CONST_VAR_H
//性能分析
#define EDRSTORE_CONST_VAR_H // 启用性能分析

#include <bits/stdc++.h>

// performance breakdown
#define EDR_BREAKDOWN

// chunk type     数据块设置
static const size_t MAX_CHUNK_SIZE = (1 << 14);// 16KB (最大分块大小)

// WrappedChunk_t contains a full encrypted chunk.  A 4096-entry queue costs
// tens of MiB and each upload creates several such queues.  Keeping the queue
// bounded at 1024 preserves FIFO semantics while preventing per-connection
// memory from accumulating during long multi-version uploads.
static const uint32_t CHUNK_QUEUE_SIZE = 1024;
static const size_t THREAD_STACK_SIZE = (8*1024*1024);// 8MB (线程栈大小)

// sketch & super-feature & feature settings  特征提取系统
static const uint32_t SUPER_FEATURE_PER_CHUNK = 5; // 3 super-feature per chunk    每个块生成3个超级特征
static const uint32_t FEATURE_PER_SUPER_FEATURE = 4; // 4 features per super-feature  每个超级特征包含4个特征
static const uint32_t FEATURE_PER_CHUNK = SUPER_FEATURE_PER_CHUNK *
    FEATURE_PER_SUPER_FEATURE; // 12 total features per chunk  每个块共12个特征

// 第二阶段密文相似性索引：一个feature最多保留N个块ID。
// 插入第N+1个不同ID时，按FIFO淘汰最老ID。
static const size_t CIPHER_FEATURE_MAX_ID_NUM = 5;

// 候选ID至少命中多少个super feature才判定为相似块。
// 低于阈值（包括只能命中已淘汰ID）时按NON_SIMILAR_CHUNK处理。
static const uint32_t CIPHER_FEATURE_MATCH_THRESHOLD = 2;

static_assert(CIPHER_FEATURE_MAX_ID_NUM > 0,
    "CIPHER_FEATURE_MAX_ID_NUM must be greater than zero");
static_assert(CIPHER_FEATURE_MATCH_THRESHOLD > 0 &&
    CIPHER_FEATURE_MATCH_THRESHOLD <= SUPER_FEATURE_PER_CHUNK,
    "invalid CIPHER_FEATURE_MATCH_THRESHOLD");

// Persistent binary-tree candidate index. Keep the current five Finesse
// super-features and threshold; only the candidate lookup structure changes.
static const uint32_t BINARY_TREE_INDEX_DEPTH = 20;
static const size_t MAX_CANDIDATES_PER_LEAF = 8;
static const size_t MAX_BINARY_TREE_CANDIDATES_PER_CHUNK = 64;
static const size_t MAX_TREE_PATHS = 3;
static const size_t BINARY_TREE_LEAF_CACHE_MAX_BYTES =
    256ULL * 1024ULL * 1024ULL;

// The storage server opens the fingerprint DB plus four tree DBs. Bound each
// LevelDB cache so their aggregate does not recreate the previous OOM issue.
static const size_t LEVELDB_BLOCK_CACHE_MAX_BYTES =
    64ULL * 1024ULL * 1024ULL;

static_assert(BINARY_TREE_INDEX_DEPTH > 0 && BINARY_TREE_INDEX_DEPTH < 63,
    "BINARY_TREE_INDEX_DEPTH must be in [1, 62]");
static_assert(MAX_CANDIDATES_PER_LEAF > 0,
    "MAX_CANDIDATES_PER_LEAF must be greater than zero");
static_assert(MAX_BINARY_TREE_CANDIDATES_PER_CHUNK > 0,
    "MAX_BINARY_TREE_CANDIDATES_PER_CHUNK must be greater than zero");
static_assert(MAX_TREE_PATHS > 0 &&
    MAX_TREE_PATHS <= SUPER_FEATURE_PER_CHUNK,
    "MAX_TREE_PATHS must be in [1, SUPER_FEATURE_PER_CHUNK]");
static_assert(BINARY_TREE_LEAF_CACHE_MAX_BYTES > 0,
    "BINARY_TREE_LEAF_CACHE_MAX_BYTES must be greater than zero");
static_assert(LEVELDB_BLOCK_CACHE_MAX_BYTES > 0,
    "LEVELDB_BLOCK_CACHE_MAX_BYTES must be greater than zero");

// for rabin fingerprint  // Rabin指纹多项式（指纹算法）
static const uint64_t FINGERPRINT_PT = 0xbfe6b8a5bf378d83LL;

// data type enum           数据类型：定义块的不同类型（普通块、压缩块、配方块等）
enum DATA_TYPE_SET {NORMAL_CHUNK = 0, COMPRESSED_NORMAL_CHUNK, RECIPE_CHUNK,
    FULL_EDR_CACHE_CHUNK, FULL_EDR_UNCOMPRESS_CHUNK, CACHE_RESTORE_DELTA,
    CACHE_RESTORE_BASE, COMP_NORMAL_CHUNK, UNCOMP_NORMAL_CHUNK};

//--------------加密算法
// for crypto info   // 支持的AES模式
enum ENCRYPT_SET {AES_256_GCM = 0, AES_128_GCM, AES_256_CFB, AES_128_CFB,
    AES_256_CBC, AES_128_CBC, AES_256_CTR, AES_128_CTR, AES_256_ECB, AES_128_ECB};
//// 默认使用AES-256-CTR
static const int CIPHER_TYPE = AES_256_CTR;

//-------------哈希算法
enum HASH_SET {SHA_256 = 0, MD5 = 1, SHA_1 = 2};
static const uint32_t CRYPTO_BLOCK_SIZE = 16;
static const uint32_t CHUNK_HASH_SIZE = 32;   // 哈希值长度(字节)
static const int HASH_TYPE = SHA_256;// 默认SHA-256


// the type of compression type  压缩设置
enum COMPRESS_TYPE_SET {LZ4_COMPRESS_TYPE = 0, ZSTD_COMPRESS_TYPE, ZLIB_COMPRESS_TYPE};
static const int COMPRESSION_TYPE = ZSTD_COMPRESS_TYPE; // 默认Zstandard
static const int COMPRESSION_LEVEL = -1;    // 默认压缩级别

// the setting of the container
static const uint32_t MAX_CONTAINER_SIZE = (1 << 22); // container size: 4MB  // 4MB (容器大小)
static const uint32_t CONTAINER_ID_LENGTH = 8; // 8 bytes   // 容器ID长度(8字节)

// the operation type   // 上传/下载
enum CLIENT_OPT_TYPE_SET {UPLOAD_OPT = 0, DOWNLOAD_OPT};

// for SSL connection     网络连接

static const int IN_SERVER_SIDE = 0;  // 服务端标识
static const int IN_CLIENT_SIDE = 1;// 客户端标识

// for network protocol   协议指令
enum NETWORK_PROTOCOL_SET {CLIENT_LOGIN_UPLOAD = 0, CLIENT_LOGIN_DOWNLOAD,
    SERVER_LOGIN_RESPONSE, CLIENT_UPLOAD_CHUNK, CLIENT_UPLOAD_RECIPE,
    CLIENT_UPLOAD_RECIPE_END, SERVER_FILE_NON_EXIST, SERVER_RESTORE_RECIPE,
    CLIENT_RESTORE_READY, CLIENT_KEY_GEN, KEY_MANAGER_KEY_GEN_REPLY,
    CLIENT_RESTORE_RECIPE_REPLY, SERVER_RESTORE_CHUNK, SERVER_RESTORE_FINAL,
    CLIENT_UPLOAD_FEATURE};

//块状态类型
enum CHUNK_STATUS_SET {UNIQUE_CHUNK = 0, UNIQUE_CHUNK_AFTER_CACHE, DUPLICATE_CHUNK, SIMILAR_CHUNK,
    NON_SIMILAR_CHUNK, COMP_DELTA_CHUNK, UNCOMP_DELTA_CHUNK, COMP_BASE_CHUNK,
    UNCOMP_BASE_CHUNK, CACHE_INSERT_CHUNK, CACHE_DELTA_CHUNK, CACHE_EVICT_CHUNK,
    MULTI_LEVEL_DELTA_CHUNK, CHUNK_PAIR, SINGLE_CHUNK};

// for SSL connection   // SSL/TLS证书路径
static const char SERVER_CERT[] = "../key/server/server.crt";
static const char SERVER_KEY[] = "../key/server/server.key";
static const char CLIENT_CERT[] = "../key/client/client.crt";
static const char CLIENT_KEY[] = "../key/client/client.key";
static const char CA_CERT[] = "../key/ca/ca.crt";

// max client    // 最大客户端连接数
static const int MAX_CLIENT_NUM = 10;

//加密方案                    明文存储     服务器辅助加密     加密后压缩
enum CLIENT_ENC_TYPE_SET {PLAIN = 0, SERVER_AIDED_MLE, ENC_COMP_MLE};

//系统架构                     仅相似加密         相似加密+压缩     完整EDR方案
enum EDR_DESIGN_SET {ONLY_SIMILAR_ENC = 0, SIMILAR_ENC_COMP, FULL_EDR};

//加密块大小计算 // 原始块 + AES块大小 + 长度字段
static const size_t ENC_MAX_CHUNK_SIZE = MAX_CHUNK_SIZE + CRYPTO_BLOCK_SIZE +
    sizeof(uint32_t);



#endif
