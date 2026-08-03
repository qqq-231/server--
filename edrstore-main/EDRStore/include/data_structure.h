/**
 * @file data_structure.h
 * @author Zhao Jia
 * @brief define the data structure in deduplication
 * @version 0.1
 * @date 2021-8
 * 
 * @copyright Copyright (c) 2021
 * 
 */

  //---------------------------------------------
 //定义了一个安全存储系统（EDRStore）中使用的各种核心数据结构
 //它包含了系统中不同模块间传输的数据结构、存储格式和网络通信格式的定义
 //---------------------------------------------

#ifndef EDRSTORE_DATA_STRUCTURE_H
#define EDRSTORE_DATA_STRUCTURE_H

#include "const_var.h"
#include <bits/stdc++.h>


//分块处理相关结构-----用于文件分块阶段的原始数据处理
typedef struct {
    uint32_t size;
    uint8_t data[MAX_CHUNK_SIZE];// 原始数据块
    uint8_t fp[CHUNK_HASH_SIZE];// 数据块指纹(SHA-256)
} RawChunk_t;

typedef struct {
    uint64_t size;// 文件总大小
    uint64_t chunk_num;// 文件包含的块数量
} FileRecipeHead_t;

typedef struct {
    union {
        RawChunk_t raw_chunk; // 原始数据块
        FileRecipeHead_t head;// 文件元数据头
    };
    int type;// 类型标识(数据块或文件头)
} Chunk_t;


typedef Chunk_t Chunker2KeyGen_t;


//特征提取结构-----存储从数据块提取的特征（用于相似性检测）
typedef struct {
    Chunk_t chunk;
    uint64_t features[SUPER_FEATURE_PER_CHUNK];    // 特征向量
} FeatureChunk_t;


// 加密处理结构------包含加密数据和相关元数据
typedef struct {
    FeatureChunk_t feature_chunk;
    uint8_t enc_data[ENC_MAX_CHUNK_SIZE]; // 加密后的数据
    uint32_t enc_size;                     // 加密数据大小
    uint8_t key[CHUNK_HASH_SIZE];            // 加密密钥
    uint64_t seed;                          // 随机种子
} EncFeatureChunk_t;

typedef EncFeatureChunk_t KeyGen2SelectComp_t;

//网络传输结构-------用于网络传输的加密数据包格式

typedef struct {
    uint32_t size;// 数据大小
    uint8_t type;// 块类型
    uint64_t cipher_features[SUPER_FEATURE_PER_CHUNK];// 加密特征
    uint8_t compressed_fp[CHUNK_HASH_SIZE];// 压缩指纹
} SendChunkHeader_t;

typedef struct {
    SendChunkHeader_t header;
    uint8_t data[ENC_MAX_CHUNK_SIZE];// 加密数据
} SendChunk_t;

//密钥管理结构-----密钥生成和管理的相关结构

typedef struct {
    uint8_t key[CHUNK_HASH_SIZE];// 加密密钥
} KeyRecipe_t;

typedef struct {
    SendChunk_t send_chunk;
    KeyRecipe_t key_recipe;
} SelectComp2Sender_t;

typedef struct {
    uint8_t key_seed[CHUNK_HASH_SIZE]; // 密钥种子
    // uint64_t seed;
} KeyGenRet_t;

typedef struct {
    // uint8_t fp[CHUNK_HASH_SIZE];
    uint64_t features[SUPER_FEATURE_PER_CHUNK]; // 特征向量
} KeyGenReq_t;




typedef struct {
    uint8_t hash[CHUNK_HASH_SIZE];
    uint8_t key[CHUNK_HASH_SIZE];
} RecipeEntry_t;


// 存储系统结构------------数据存储的容器格式和索引结构

typedef struct {
    uint8_t stat;// 状态标志
    uint8_t container_id[CONTAINER_ID_LENGTH];// 容器ID
    uint32_t offset; // 容器内偏移
    uint32_t len;// 数据长度
    uint8_t base_fp[CHUNK_HASH_SIZE];// 基础块指纹
    uint8_t compressed_fp[CHUNK_HASH_SIZE];// 压缩块指纹
} KeyForChunkHashDB_t;

typedef struct {
    char id[CONTAINER_ID_LENGTH];// 容器ID
    uint8_t body[MAX_CONTAINER_SIZE];// 容器数据体
    uint32_t cur_size; // 当前大小
} Container_t;


//网络通信结构--------网络通信的消息头格式
typedef struct {
    uint32_t client_id; // 客户端ID
    uint32_t msg_type; // 消息类型
    uint32_t cur_item_num; // 当前项数量
    uint32_t size; // 消息大小
} NetworkHead_t;

typedef struct {
    NetworkHead_t* header;// 网络头
    uint8_t* send_buf;  // 发送缓冲区
    uint8_t* data_buf;// 数据缓冲区
} SendMsgBuffer_t;


// 元数据结构--------------块的元数据信息

typedef struct {
    uint8_t fp[CHUNK_HASH_SIZE]; // chunk fp // 块指纹
    // uint8_t compressed_fp[CHUNK_HASH_SIZE];
    uint64_t features[SUPER_FEATURE_PER_CHUNK];// 特征向量
    KeyForChunkHashDB_t addr; // 存储地址信息
    uint32_t size;// 块大小
    // Original encrypted size while a tentative local Delta waits for the
    // client's compressed representation.
    uint32_t logical_size;
    uint8_t stat; // 状态标志
    uint8_t compressed_fp[CHUNK_HASH_SIZE]; // 客户端计算的加密压缩数据的哈希
} ChunkInfo_t;

typedef struct {
    ChunkInfo_t info;// 块元信息
    uint8_t data[ENC_MAX_CHUNK_SIZE];// 加密数据
} WrappedChunk_t;



typedef struct {
    uint8_t* buf;
    uint32_t cnt;
} BatchBuf_t;// 批量缓冲区

// 客户端明文缓冲区条目（用于补发加密压缩数据）
typedef struct {
    uint8_t plaintext[MAX_CHUNK_SIZE];
    uint32_t plaintext_size;
    uint8_t key[CHUNK_HASH_SIZE];
    uint64_t seed;
} PlaintextEntry_t;

// 客户端→服务器补发响应
typedef struct {
    uint8_t compressed_fp[CHUNK_HASH_SIZE];
    uint32_t enc_comp_size;
    uint8_t enc_comp_data[ENC_MAX_CHUNK_SIZE];
} RecompressResponse_t;

typedef struct {
    SendChunk_t input_chunk;
    SendChunk_t base_chunk;
} Reader2Decoder_t;//阅读器到解码器的传递结构。

typedef SendChunk_t Retriever2Writer_t;

// 下载进度追踪结构（断点续传）
typedef struct {
    uint8_t file_name_hash[CHUNK_HASH_SIZE];  // 文件标识哈希
    uint64_t total_chunks;                     // 期望总chunk数
    uint64_t received_chunks;                  // 已完成chunk数
    uint64_t received_bytes;                   // 已完成字节数
} DownloadProgress_t;

#endif
