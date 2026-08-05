/**
 * @file cache_meta.cc
 * @author Zhao Jia
 * @brief
 * @version 0.1
 * @date 2022-06-20
 *
 * @copyright Copyright (c) 2022
 *
 */

#include "../../include/client/cache_meta.h"

/**
 * @brief Construct a new CacheMeta object
 *
 */
CacheMeta::CacheMeta() {
    this->LoadCacheMeta();
}

/**
 * @brief Destroy the CacheMeta object
 *
 */
CacheMeta::~CacheMeta(){
    this->StoreCacheMeta();
}

/**
 * @brief load cache metadata
 *
 */
void CacheMeta::LoadCacheMeta() {
    // check cache meta data exist
    ifstream cache_meta_hdl;
    if (tool::FileExist(db_name_)) {
        // check the file size
        cache_meta_hdl.open(db_name_, ios_base::in | ios_base::binary);
        if (!cache_meta_hdl.is_open()) {
            tool::Logging(my_name_.c_str(), "cannot open the cache meta.\n");
            exit(EXIT_FAILURE);
        }

        size_t start_size = cache_meta_hdl.tellg();
        cache_meta_hdl.seekg(0, ios_base::end);
        size_t file_size = cache_meta_hdl.tellg();
        file_size = file_size - start_size;

        if (file_size == 0) {
            return ;
        }
        cache_meta_hdl.seekg(0, ios_base::beg);

        cache_meta_hdl.read((char*)&_total_similar_chunk, sizeof(uint64_t));

        // read feature set
        size_t idx_item_num = 0;
        cache_meta_hdl.read((char*)&idx_item_num, sizeof(size_t));
        if (idx_item_num == 0) {
            return ;
        }

        uint64_t feature;
        for (size_t i = 0; i < idx_item_num; i++) {
            cache_meta_hdl.read((char*)&feature, sizeof(uint64_t));
            feature_set_.insert(feature);
        }

        cache_meta_hdl.close();
    }

    return ;
}

/**
 * @brief store cache metadata
 *
 */
void CacheMeta::StoreCacheMeta() {
    ofstream cache_meta_hdl;
    cache_meta_hdl.open(db_name_, ios_base::trunc | ios_base::binary);
    if (!cache_meta_hdl.is_open()) {
        tool::Logging(my_name_.c_str(), "cannot init the cache meta.\n");
        exit(EXIT_FAILURE);
    }

    cache_meta_hdl.write((char*)&_total_similar_chunk, sizeof(uint64_t));

    size_t idx_item_num = feature_set_.size();
    cache_meta_hdl.write((char*)&idx_item_num, sizeof(size_t));

    for (auto feature : feature_set_) {
        cache_meta_hdl.write((char*)&feature, sizeof(uint64_t));
    }
    cache_meta_hdl.close();

    return ;
}

/**
 * @brief insert the features to the cache
 *
 * @param features input features
 */
void CacheMeta::UpdateCacheMeta(uint64_t* features) {
    for (size_t i = 0; i < SUPER_FEATURE_PER_CHUNK; i++) {
        feature_set_.insert(features[i]);
    }
    return ;
}

/**
 * @brief query the cache meta to check whether it is similar?
 *
 * @param features input features
 * @return true it is similar chunk
 * @return false it is non-similar chunk
 */
bool CacheMeta::QueryCacheMeta(uint64_t* features) {
    bool is_similar = false;
    for (size_t i = 0; i < SUPER_FEATURE_PER_CHUNK; i++) {
        if (feature_set_.find(features[i]) != feature_set_.end()) {
            is_similar = true;
        }
    }
    if (is_similar) {
        _total_similar_chunk++;
    }

    return is_similar;
}
