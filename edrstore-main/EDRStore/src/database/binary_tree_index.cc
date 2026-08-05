/**
 * @file binary_tree_index.cc
 * @brief Implement the full binary tree index for block-level deduplication
 *
 * Reference: CSP full binary tree for secure deduplication
 *
 * @version 0.1
 * @date 2023
 */

#include "../../include/database/binary_tree_index.h"

/**
 * @brief Construct with initial tree height
 */
BinaryTreeIndex::BinaryTreeIndex(uint32_t initial_height) {
    max_height_ = initial_height;
    num_leaves_ = uint32_t(1) << max_height_;       // 2^h leaves
    uint32_t total_nodes = (uint32_t(1) << (max_height_ + 1)); // 2^(h+1) nodes (1-indexed)
    leaf_base_ = num_leaves_;                        // leaf indices: leaf_base_ .. leaf_base_+num_leaves_-1

    // allocate 1-indexed array
    tree_.resize(total_nodes);

    // pre-populate free leaf queue
    for (uint32_t i = leaf_base_; i < leaf_base_ + num_leaves_; i++) {
        free_leaves_.push(i);
    }

    pthread_rwlock_init(&rwlock_, NULL);

    tool::Logging(my_name_.c_str(), "init tree: height=%u, leaves=%u, total_nodes=%u\n",
        max_height_, num_leaves_, total_nodes - 1);
}

/**
 * @brief Destroy
 */
BinaryTreeIndex::~BinaryTreeIndex() {
    pthread_rwlock_destroy(&rwlock_);
}

/**
 * @brief Expand the tree by one level (doubles leaf capacity)
 */
void BinaryTreeIndex::ExpandTree() {
    uint32_t new_height = max_height_ + 1;
    uint32_t new_total_nodes = (uint32_t(1) << (new_height + 1));
    uint32_t new_leaf_base = (uint32_t(1) << new_height);
    uint32_t new_num_leaves = new_leaf_base;

    tool::Logging(my_name_.c_str(), "expanding tree: height %u -> %u, leaves %u -> %u\n",
        max_height_, new_height, num_leaves_, new_num_leaves);

    // resize the tree array (preserving old nodes)
    tree_.resize(new_total_nodes);

    // add new free leaves
    for (uint32_t i = new_leaf_base; i < new_leaf_base + new_num_leaves; i++) {
        free_leaves_.push(i);
    }

    max_height_ = new_height;
    num_leaves_ = new_num_leaves;
    leaf_base_ = new_leaf_base;
}

/**
 * @brief Insert a block tag into the tree
 */
uint32_t BinaryTreeIndex::InsertBlockTag(const uint8_t* fp, size_t key_size) {
    pthread_rwlock_wrlock(&rwlock_);

    // check if already exists (O(1) via hash map)
    string fp_str;
    fp_str.assign((const char*)fp, key_size);
    auto it = tag_to_pos_.find(fp_str);
    if (it != tag_to_pos_.end()) {
        uint32_t existing_pos = it->second;
        pthread_rwlock_unlock(&rwlock_);
        return existing_pos;
    }

    // need to insert — check for free leaf
    if (free_leaves_.empty()) {
        // tree full, expand
        ExpandTree();
    }

    uint32_t leaf_pos = free_leaves_.front();
    free_leaves_.pop();

    // store the tag at the leaf node (use CHUNK_HASH_SIZE for fixed-width node storage)
    memcpy(tree_[leaf_pos].tag, fp, CHUNK_HASH_SIZE);
    tree_[leaf_pos].is_occupied = true;

    // update hash map with exact key bytes
    tag_to_pos_[fp_str] = leaf_pos;

    pthread_rwlock_unlock(&rwlock_);
    return leaf_pos;
}

/**
 * @brief Find leaf position for a given tag (O(1) via hash map)
 */
uint32_t BinaryTreeIndex::FindLeafPosition(const uint8_t* fp, size_t key_size) {
    pthread_rwlock_rdlock(&rwlock_);
    string fp_str;
    fp_str.assign((const char*)fp, key_size);
    auto it = tag_to_pos_.find(fp_str);
    uint32_t result = (it != tag_to_pos_.end()) ? it->second : 0;
    pthread_rwlock_unlock(&rwlock_);
    return result;
}

/**
 * @brief Check if a tag exists
 */
bool BinaryTreeIndex::QueryTag(const uint8_t* fp, size_t key_size) {
    return FindLeafPosition(fp, key_size) != 0;
}

/**
 * @brief Insert (key, value) pair — compatible with AbsDatabase interface
 *        key = fingerprint, value = address data
 */
bool BinaryTreeIndex::InsertBothBuffer(const char* key, size_t key_size,
    const char* buf, size_t buf_size) {
    uint32_t pos = InsertBlockTag((const uint8_t*)key, key_size);
    // pos > 0: leaf position assigned (either new or existing)
    return (pos > 0);
}

/**
 * @brief Query key — compatible with AbsDatabase interface
 */
bool BinaryTreeIndex::QueryBuffer(const char* key, size_t key_size, string& value) {
    uint32_t pos = FindLeafPosition((const uint8_t*)key, key_size);
    if (pos == 0) {
        return false;
    }
    // Return leaf position encoded as value (caller interprets)
    value.assign((const char*)&pos, sizeof(uint32_t));
    return true;
}
