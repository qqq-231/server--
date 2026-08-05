//------------------------------------------------------
// 定义了一个配置管理类 Configure，用于从JSON配置文件中读取和管理系统的运行时参数
//-----------------------------------------------------------

/**
 * @file configure.h
 * @author Zuoru Yang
 * @brief define the necessary variables in deduplication
 * @version 0.1
 * @date 2021-8
 * 
 * @copyright Copyright (c) 2021
 * 
 */

#ifndef EDRSTORE_CONFIGURE_H
#define EDRSTORE_CONFIGURE_H

#include <bits/stdc++.h>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include "const_var.h"
#include "define.h"

using namespace std;

class Configure {
    private:
        string my_name_ = "Configure";
        // chunking settings  分块设置
        uint64_t chunking_type_;   // 分块算法类型
        uint64_t max_chunk_size_;   // 最大块大小 (默认16KB)
        uint64_t min_chunk_size_;  // 最小块大小
        uint64_t avg_chunk_size_;     // 平均块大小
        uint64_t chunker_sliding_win_size_;// 滑动窗口大小
        uint64_t read_size_; //128M per time   // 文件读取粒度 (默认128MB)
        uint64_t mask;



        // similar config   // 相似性检测窗口大小
        uint64_t similar_sliding_win_size_;

        // storage server settings  存储服务器
        string storage_server_ip_;// 存储服务器IP
        int storage_server_port_;// 存储服务器端口
        string recipe_root_path_; // 配方存储路径
        string container_root_path_;// 容器存储路径
        string cache_root_path_; // 缓存路径
        string fp_2_chunk_db_; // 指纹->块数据库名
        string feature_2_fp_db_;// 特征->指纹数据库名
        uint64_t container_cache_size_;// 容器缓存大小

        string feature_1;
        string feature_2;

        // key manager settings  密钥管理
        string km_ip_;  // 密钥服务器IP
        int km_port_;  // 密钥服务器端口
        string feature_2_key_db_;// 特征->密钥数据库名

        // client settings 客户端设置
        uint32_t client_id_; // 客户端ID
        uint64_t send_chunk_batch_size_;// 块批量发送大小
        uint64_t send_recipe_batch_size_; // 配方批量发送大小
        string user_key_; // 用户密钥

        // const 文件后缀
        string recipe_suffix_ = "-recipe"; // 配方文件后缀
        string container_suffix_ = "-container";// 容器文件后缀
        string key_recipe_suffix_ = "-key"; // 密钥配方后缀

        /**
         * @brief parse the json file
         * 
         * @param path the path to the json file  配置加载
         */
        void ReadConfig(string path); // 解析JSON配置文件

    public:

        /**
         * @brief Construct a new Configure object
         * 
         * @param path the path to the json file
         */
        Configure(string path);  // 初始化时加载配置

        /**
         * @brief Destroy the Configure object
         * 
         */
        ~Configure();

        // chunking settings   参数访问接口  --getter方法
        uint64_t GetChunkingType() {
            return chunking_type_;
        }
        uint64_t GetMaxChunkSize() {
            return max_chunk_size_;
        }
        uint64_t GetMinChunkSize() {
            return min_chunk_size_;
        }
        uint64_t GetAvgChunkSize() {
            return avg_chunk_size_;
        }
        uint64_t GetChunkerSlidingWinSize() {
            return chunker_sliding_win_size_;
        }
        uint64_t GetReadSize() {
            return read_size_;
        }

        uint64_t GetMask() {
            return mask;
        }



    string Getfeature_1() {
            return feature_1;
        }

    string Getfeature_2() {
            return feature_2;
        }



        // similar detection setting
        uint64_t GetSimilarSlidingWinSize() {
            return similar_sliding_win_size_;
        }

        // storage management settings
        string GetStorageServerIP() {
            return storage_server_ip_;
        }
        int GetStorageServerPort() {
            return storage_server_port_;
        }
        string GetRecipeRootPath() {
            return recipe_root_path_;
        }
        string GetContainerRootPath() {
            return container_root_path_;
        }
        string GetCacheRootPath() {
            return cache_root_path_;
        }
        string GetFp2ChunkDBName() {
            return fp_2_chunk_db_;
        }
        string GetFeature2FpDBName() {
            return feature_2_fp_db_;
        }
        uint64_t GetContainerCacheSize() {
            return container_cache_size_;
        }

        // key management settings
        string GetKeyServerIP() {
            return km_ip_;
        }
        int GetKeyServerPort() {
            return km_port_;
        }
        string GetFeature2KeyDBName() {
            return feature_2_key_db_;
        }

        // client settings
        uint32_t GetClientID() {
            return client_id_;
        }
        void SetClientID(uint32_t id) {
            client_id_ = id;
        }
        uint64_t GetSendChunkBatchSize() {
            return send_chunk_batch_size_;
        }
        uint64_t GetSendRecipeBatchSize() {
            return send_recipe_batch_size_;
        }
        string GetUserKey() {
            return user_key_;
        }

        // global
        string GetRecipeSuffix() {
            return recipe_suffix_;
        }
        string GetContainerSuffix() {
            return container_suffix_;
        }
        string GetKeyRecipeSuffix() {
            return key_recipe_suffix_;
        }
};

#endif