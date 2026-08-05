/**
 * @file binary_tree_index.h
 * @brief Full binary tree index for block-level deduplication
 *
 * Implements a full binary tree (heap-like array) where leaf nodes store
 * chunk fingerprints (block tags). A hash map provides O(1) tag→position lookup.
 * Files are represented as paths (sequences of leaf positions).
 *
 * Reference: CSP full binary tree for secure deduplication
 *
 * @version 0.1
 * @date 2023
 */

#ifndef EDRSTORE_BINARY_TREE_INDEX_H
#define EDRSTORE_BINARY_TREE_INDEX_H

#include "../const_var.h"
#include "../define.h"
#include "../data_structure.h"
#include <queue>

using namespace std;

// forward declaration for friend access
class BinaryTreeDB;

class BinaryTreeIndex {
    friend class BinaryTreeDB;

    private:
        string my_name_ = "BinaryTreeIndex";

    public:
        // Tree node (only leaf nodes store tag data)
        struct TreeNode {
            uint8_t tag[CHUNK_HASH_SIZE];   // block fingerprint (leaf only)
            bool is_occupied;                // whether this leaf has valid data
            TreeNode() : is_occupied(false) {
                memset(tag, 0, CHUNK_HASH_SIZE);
            }
        };

    private:

        // 1-indexed array (heap structure): root at 1, left child = 2*i, right child = 2*i+1
        vector<TreeNode> tree_;

        // Hash map: fingerprint string → leaf position (O(1) lookup)
        unordered_map<string, uint32_t> tag_to_pos_;

        // Free leaf positions for O(1) allocation
        queue<uint32_t> free_leaves_;

        // Tree parameters
        uint32_t max_height_;    // current tree height (levels 0..max_height_)
        uint32_t num_leaves_;    // total leaf capacity = 2^max_height_
        uint32_t leaf_base_;     // first leaf index = 2^max_height_

        // Thread safety
        pthread_rwlock_t rwlock_;

        /**
         * @brief Expand the tree by increasing height by 1 (doubles leaf capacity)
         */
        void ExpandTree();

        /**
         * @brief Check if a node index is a leaf
         */
        inline bool IsLeaf(uint32_t pos) {
            return pos >= leaf_base_;
        }

        /**
         * @brief Get the parent of a node
         */
        inline uint32_t GetParent(uint32_t pos) {
            return pos / 2;
        }

        /**
         * @brief Get left child
         */
        inline uint32_t GetLeftChild(uint32_t pos) {
            return 2 * pos;
        }

        /**
         * @brief Get right child
         */
        inline uint32_t GetRightChild(uint32_t pos) {
            return 2 * pos + 1;
        }

    public:
        /**
         * @brief Construct with initial tree height
         * @param initial_height Initial tree height (default 10 → 1024 leaves)
         */
        BinaryTreeIndex(uint32_t initial_height = 10);

        /**
         * @brief Destroy and clean up
         */
        ~BinaryTreeIndex();

        /**
         * @brief Insert a block tag (fp) into the tree, returning leaf position
         *        If tag already exists, return its existing position.
         * @param fp Chunk fingerprint
         * @param key_size Actual key size in bytes (e.g. CHUNK_HASH_SIZE for fp, sizeof(uint64_t) for features)
         * @return Leaf position (1-indexed), 0 on failure
         */
        uint32_t InsertBlockTag(const uint8_t* fp, size_t key_size);

        /**
         * @brief Query whether a tag exists, returning its leaf position
         * @param fp Chunk fingerprint
         * @param key_size Actual key size in bytes
         * @return Leaf position if found, 0 if not found
         */
        uint32_t FindLeafPosition(const uint8_t* fp, size_t key_size);

        /**
         * @brief Check if a tag exists in the tree
         * @param fp Chunk fingerprint
         * @param key_size Actual key size in bytes
         * @return true if exists
         */
        bool QueryTag(const uint8_t* fp, size_t key_size);

        /**
         * @brief Insert a (key, value) pair using raw buffers
         *        Key = fp, value = address data
         * @param key Fingerprint buffer
         * @param key_size Key size
         * @param buf Value buffer
         * @param buf_size Value size
         * @return true if already existed
         */
        bool InsertBothBuffer(const char* key, size_t key_size,
            const char* buf, size_t buf_size);

        /**
         * @brief Query a key, returning value
         * @param key Fingerprint buffer
         * @param key_size Key size
         * @param value Output value string
         * @return true if found
         */
        bool QueryBuffer(const char* key, size_t key_size, string& value);

        /**
         * @brief Get current number of occupied leaves
         */
        uint32_t GetOccupiedCount() {
            return tag_to_pos_.size();
        }

        /**
         * @brief Get current tree height
         */
        uint32_t GetMaxHeight() {
            return max_height_;
        }

        /**
         * @brief Get total leaf capacity
         */
        uint32_t GetLeafCapacity() {
            return num_leaves_;
        }

        /**
         * @brief Get reference to tree node array (for persistence)
         */
        inline vector<TreeNode>& GetTree() {
            return tree_;
        }

        /**
         * @brief Get first leaf index
         */
        inline uint32_t GetLeafBase() const {
            return leaf_base_;
        }

        /**
         * @brief Get number of leaves
         */
        inline uint32_t GetNumLeaves() const {
            return num_leaves_;
        }
};

#endif
