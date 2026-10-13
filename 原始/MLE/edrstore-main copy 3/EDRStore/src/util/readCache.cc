/**
 * @file readCache.cc
 * @author Zuoru YANG (zryang@cse.cuhk.edu.hk)
 * @brief implement the interface defined in readCache.h
 * @version 0.1
 * @date 2020-02-07
 * 
 * @copyright Copyright (c) 2020
 * 
 */

#include "../../include/readCache.h"
#include <boost/thread/mutex.hpp>

/**
 * @brief Construct a new Read Cache object
 * 
 * @param itemNum the number of the item
 * @param itemSize the size of each item
 */
ReadCache::ReadCache(size_t itemNum, size_t itemSize) {
    cacheSize_ = itemNum;
    itemSize_ = itemSize;
    this->readCache_ = new lru11::Cache<string, uint32_t>(cacheSize_, 0);
    // Preserve the configured number of cache entries, but allocate each
    // 4 MiB container slot only when it is first populated.
    containerPool_ = (uint8_t**) calloc(cacheSize_, sizeof(uint8_t*));
    currentIndex_ = 0;
}

/**
 * @brief Destroy the Read Cache object
 * 
 */
ReadCache::~ReadCache() {
    for (size_t i = 0; i < cacheSize_; i++) {
        if (containerPool_[i] != nullptr) {
            free(containerPool_[i]);
        }
    }
    free(containerPool_);
    delete readCache_;
}

/**
 * @brief insert the data to the cache
 * 
 * @param name key - container ID
 * @param data data
 * @param length the length of the container section
 */
void ReadCache::Insert(string& name, uint8_t* data, uint32_t length) {
    if (length > itemSize_) {
        fprintf(stderr, "ReadCache: item length %u exceeds slot size %zu.\n",
            length, itemSize_);
        return ;
    }

    if (readCache_->size() + 1 > cacheSize_) {
        // evict a item
        uint32_t replaceIndex = readCache_->pruneValue();
        if (containerPool_[replaceIndex] == nullptr) {
            containerPool_[replaceIndex] =
                (uint8_t*) malloc(itemSize_ * sizeof(uint8_t));
        }
        memset(containerPool_[replaceIndex], 0, itemSize_);
        memcpy(containerPool_[replaceIndex], data, length);
        readCache_->insert(name, replaceIndex);
    } else {
        if (containerPool_[currentIndex_] == nullptr) {
            containerPool_[currentIndex_] =
                (uint8_t*) malloc(itemSize_ * sizeof(uint8_t));
        }
        memset(containerPool_[currentIndex_], 0, itemSize_);
        memcpy(containerPool_[currentIndex_], data, length);
        readCache_->insert(name, currentIndex_);
        currentIndex_++;
    }
    return ;
}

/**
 * @brief check whether this item exists in the cache
 * 
 * @param name 
 * @return true 
 * @return false 
 */
bool ReadCache::Exist(string& name) {
    bool flag = false;
    flag = this->readCache_->contains(name);
    return flag;
}

/**
 * @brief read the data from the cache
 * 
 * @param data key
 * @return string the data
 */
uint8_t* ReadCache::Read(string& name) {
    uint32_t index = this->readCache_->get(name);
    return containerPool_[index];
}
