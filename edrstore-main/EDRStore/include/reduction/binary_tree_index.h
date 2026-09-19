/**
 * @file binary_tree_index.h
 * @brief Persistent binary-tree index for exact deduplication paths and
 *        similarity-candidate narrowing.
 */

#ifndef EDRSTORE_BINARY_TREE_INDEX_H
#define EDRSTORE_BINARY_TREE_INDEX_H

#include "../database/db_factory.h"
#include "../data_structure.h"

// Node IDs use the usual full-binary-tree numbering: root=1, left=2*n,
// right=2*n+1. Internal nodes contain topology only; leaf metadata points to
// the chunks stored in the leaf database.
typedef struct {
    uint64_t node_id;
    uint64_t parent;
    uint64_t left_child;
    uint64_t right_child;
    uint32_t level;
    uint64_t position;
    uint8_t is_leaf;
    uint8_t has_block_tag;
    uint64_t block_tag;
    uint8_t chunk_id[CHUNK_HASH_SIZE];
} BinaryTreeNode_t;

typedef struct {
    uint64_t block_tag;
    uint8_t chunk_id[CHUNK_HASH_SIZE];
    uint64_t features[SUPER_FEATURE_PER_CHUNK];
    KeyForChunkHashDB_t storage_location;
} BinaryTreeLeafEntry_t;

typedef struct {
    uint8_t chunk_id[CHUNK_HASH_SIZE];
    uint64_t leaf_node_id;
    uint64_t path[BINARY_TREE_INDEX_DEPTH + 1];
} BinaryTreePathRecord_t;

struct BinaryTreeCandidate {
    uint64_t block_tag;
    uint64_t leaf_node_id;
    uint8_t chunk_id[CHUNK_HASH_SIZE];
    uint64_t features[SUPER_FEATURE_PER_CHUNK];
    KeyForChunkHashDB_t storage_location;
};

struct BinaryTreeIndexStat {
    uint64_t materialized_node_num = 0;
    uint64_t leaf_bucket_num = 0;
    uint64_t leaf_entry_num = 0;
    uint64_t indexed_chunk_num = 0;
    uint64_t file_path_num = 0;
    uint64_t exact_hit_num = 0;
    uint64_t exact_miss_num = 0;
    uint64_t candidate_search_num = 0;
    uint64_t candidate_leaf_num = 0;
    uint64_t candidate_verify_num = 0;
    uint64_t candidate_returned_num = 0;
    uint64_t max_candidate_num = 0;
    uint64_t loaded_leaf_entry_num = 0;
    uint64_t max_loaded_leaf_size = 0;
    uint64_t tree_path_search_num = 0;
    uint64_t leaf_cache_hit_num = 0;
    uint64_t leaf_cache_miss_num = 0;
    uint64_t binary_tree_search_time_us = 0;
    uint64_t candidate_filtering_time_us = 0;
    uint64_t leaf_cache_entry_num = 0;
    uint64_t leaf_cache_current_bytes = 0;
    uint64_t leaf_cache_peak_bytes = 0;
    uint64_t leaf_cache_limit_bytes = 0;
    uint64_t leaf_cache_eviction_num = 0;
};

class BinaryTreeIndex {
    private:
        string my_name_ = "BinaryTreeIndex";
        AbsDatabase* node_db_;
        AbsDatabase* leaf_db_;
        AbsDatabase* chunk_path_db_;
        AbsDatabase* file_path_db_;
        mutable mutex index_mutex_;
        struct CachedLeaf {
            shared_ptr<vector<BinaryTreeLeafEntry_t>> entries;
            size_t accounted_bytes;
            list<uint64_t>::iterator lru_position;
        };
        // Depth-D leaf IDs occupy the continuous range [2^D, 2^(D+1)).
        // Index cache slots by (node_id - 2^D), avoiding a hash lookup for
        // every tree path while retaining the existing bounded LRU policy.
        vector<unique_ptr<CachedLeaf>> leaf_cache_by_position_;
        list<uint64_t> leaf_cache_lru_;
        size_t leaf_cache_entry_num_ = 0;
        size_t leaf_cache_current_bytes_ = 0;
        size_t leaf_cache_peak_bytes_ = 0;
        size_t leaf_cache_max_bytes_ = BINARY_TREE_LEAF_CACHE_MAX_BYTES;
        uint64_t leaf_cache_eviction_num_ = 0;
        // Full-tree heap IDs are continuous. A byte presence vector avoids a
        // database query for every node on paths already seen in this process.
        vector<uint8_t> node_present_cache_;

        atomic<uint64_t> exact_hit_num_{0};
        atomic<uint64_t> exact_miss_num_{0};
        atomic<uint64_t> candidate_search_num_{0};
        atomic<uint64_t> candidate_leaf_num_{0};
        atomic<uint64_t> candidate_verify_num_{0};
        atomic<uint64_t> candidate_returned_num_{0};
        atomic<uint64_t> max_candidate_num_{0};
        atomic<uint64_t> loaded_leaf_entry_num_{0};
        atomic<uint64_t> max_loaded_leaf_size_{0};
        atomic<uint64_t> tree_path_search_num_{0};
        atomic<uint64_t> leaf_cache_hit_num_{0};
        atomic<uint64_t> leaf_cache_miss_num_{0};
        atomic<uint64_t> binary_tree_search_time_us_{0};
        atomic<uint64_t> candidate_filtering_time_us_{0};
        atomic<uint64_t> materialized_node_num_{0};
        atomic<uint64_t> leaf_bucket_num_{0};
        atomic<uint64_t> leaf_entry_num_{0};
        atomic<uint64_t> indexed_chunk_num_{0};
        atomic<uint64_t> file_path_num_{0};

        uint64_t ComputeLeafNodeID(uint64_t block_tag) const;
        uint64_t EnsurePath(uint64_t block_tag, const uint8_t* chunk_id);
        void BuildPath(uint64_t block_tag,
            uint64_t path[BINARY_TREE_INDEX_DEPTH + 1]) const;
        bool LoadLeaf(uint64_t leaf_node_id,
            shared_ptr<vector<BinaryTreeLeafEntry_t>>& entries,
            bool record_cache_stat);
        void BatchLoadLeaf(const vector<uint64_t>& leaf_node_ids,
            vector<shared_ptr<vector<BinaryTreeLeafEntry_t>>>& entries,
            vector<bool>& found, bool record_cache_stat);
        size_t EstimateLeafCacheBytes(
            const vector<BinaryTreeLeafEntry_t>& entries) const;
        size_t LeafCachePosition(uint64_t leaf_node_id) const;
        void TouchCachedLeaf(uint64_t leaf_node_id,
            CachedLeaf& cached_leaf);
        void AdmitLeafToCache(uint64_t leaf_node_id,
            const shared_ptr<vector<BinaryTreeLeafEntry_t>>& entries);
        void EnforceLeafCacheLimit();
        bool IsUsableBase(const KeyForChunkHashDB_t& location) const;

    public:
        BinaryTreeIndex(AbsDatabase* node_db, AbsDatabase* leaf_db,
            AbsDatabase* chunk_path_db, AbsDatabase* file_path_db,
            size_t leaf_cache_max_bytes =
                BINARY_TREE_LEAF_CACHE_MAX_BYTES);
        ~BinaryTreeIndex();

        // Exact lookup returns the shared leaf for a previously indexed chunk.
        bool FindChunk(const uint8_t* chunk_id, uint64_t* leaf_node_id = nullptr);

        // Traverse one path per super-feature and return only bounded leaf
        // candidates. Similarity verification is deliberately left to
        // SimilarPolicy.
        void FindBaseCandidates(const uint64_t* features,
            const uint8_t* current_chunk_id,
            vector<BinaryTreeCandidate>& candidates);

        // Called only after the chunk data and fp->address metadata are stored.
        bool InsertChunk(const ChunkInfo_t* info);

        // Persist FileTag -> chunk order -> root-to-leaf path mappings.
        bool RecordFileRecipe(const string& file_tag, const string& recipe_path);

        BinaryTreeIndexStat GetStat() const;
};

#endif
