/**
 * @file cache_meta.h
 * @author Zhao Jia
 * @brief
 * @version 0.1
 * @date 2022-06-20
 *
 * @copyright Copyright (c) 2022
 *
 */

#ifndef EDRSTORE_CACHE_META_H
#define EDRSTORE_CACHE_META_H

#include "../configure.h"
#include "../define.h"

using namespace std;

extern Configure config;

class CacheMeta{
    private:
        string my_name_= "CacheMeta";
        string db_name_ = "cache_meta_db";

        // feature --> exists (bool via presence in set)
        unordered_set<uint64_t> feature_set_;

        /**
         * @brief load cache metadata
         *
         */
        void LoadCacheMeta();

        /**
         * @brief store cache metadata
         *
         */
        void StoreCacheMeta();

    public:
        // record current cache size
        uint64_t _total_similar_chunk = 0;

        /**
         * @brief Construct a new CacheMeta object
         *
         */
        CacheMeta();

        /**
         * @brief Destroy the CacheMeta object
         *
         */
        ~CacheMeta();

        /**
         * @brief insert the features to the cache
         *
         * @param features input features
         */
        void UpdateCacheMeta(uint64_t* features);

        /**
         * @brief query the cache meta to check whether it is similar?
         *
         * @param features input features
         * @return true it is similar chunk
         * @return false it is non-similar chunk
         */
        bool QueryCacheMeta(uint64_t* features);

        /**
         * @brief Get the Feature Num object
         *
         * @return size_t the num of feature
         */
        size_t GetFeatureNum() {
            return feature_set_.size();
        }
};

#endif
