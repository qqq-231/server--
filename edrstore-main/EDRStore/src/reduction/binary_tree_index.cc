/**
 * @file binary_tree_index.cc
 * @brief Binary-tree index implementation.
 */

#include "../../include/reduction/binary_tree_index.h"

namespace {
struct RankedTreeCandidate {
    BinaryTreeCandidate candidate;
    uint32_t exact_feature_matches;
    uint32_t feature_distance;
};

bool BetterTreeCandidate(const RankedTreeCandidate& left,
    const RankedTreeCandidate& right) {
    if (left.exact_feature_matches != right.exact_feature_matches) {
        return left.exact_feature_matches > right.exact_feature_matches;
    }
    if (left.feature_distance != right.feature_distance) {
        return left.feature_distance < right.feature_distance;
    }
    return memcmp(left.candidate.chunk_id, right.candidate.chunk_id,
        CHUNK_HASH_SIZE) < 0;
}

void UpdateAtomicMaximum(atomic<uint64_t>& target, uint64_t value) {
    uint64_t previous = target.load(memory_order_relaxed);
    while (previous < value && !target.compare_exchange_weak(previous, value,
        memory_order_relaxed, memory_order_relaxed)) {
    }
}

RankedTreeCandidate RankTreeCandidate(const uint64_t* query_features,
    uint64_t leaf_node_id, const BinaryTreeLeafEntry_t& entry) {
    RankedTreeCandidate ranked = {};
    ranked.candidate.block_tag = entry.block_tag;
    ranked.candidate.leaf_node_id = leaf_node_id;
    memcpy(ranked.candidate.chunk_id, entry.chunk_id, CHUNK_HASH_SIZE);
    memcpy(ranked.candidate.features, entry.features,
        sizeof(uint64_t) * SUPER_FEATURE_PER_CHUNK);
    ranked.candidate.storage_location = entry.storage_location;

    // Treat the three super-features as an unordered set. For every query
    // feature, use its closest candidate feature and sum the Hamming distance.
    for (size_t query_idx = 0; query_idx < SUPER_FEATURE_PER_CHUNK;
        query_idx++) {
        uint32_t minimum_distance = 64;
        bool exact_match = false;
        for (size_t candidate_idx = 0;
            candidate_idx < SUPER_FEATURE_PER_CHUNK; candidate_idx++) {
            const uint64_t difference = query_features[query_idx] ^
                entry.features[candidate_idx];
            const uint32_t distance = static_cast<uint32_t>(
                __builtin_popcountll(difference));
            minimum_distance = min(minimum_distance, distance);
            exact_match = exact_match || difference == 0;
        }
        ranked.feature_distance += minimum_distance;
        if (exact_match) ranked.exact_feature_matches++;
    }
    return ranked;
}

void KeepBestLeafCandidate(vector<RankedTreeCandidate>& top_candidates,
    RankedTreeCandidate&& candidate) {
    if (top_candidates.size() < MAX_CANDIDATES_PER_LEAF) {
        top_candidates.push_back(std::move(candidate));
        return;
    }

    // BetterTreeCandidate defines a best-first order, so max_element with
    // that predicate identifies the current worst item in the bounded set.
    auto worst = max_element(top_candidates.begin(), top_candidates.end(),
        BetterTreeCandidate);
    if (BetterTreeCandidate(candidate, *worst)) {
        *worst = std::move(candidate);
    }
}
}

BinaryTreeIndex::BinaryTreeIndex(AbsDatabase* node_db, AbsDatabase* leaf_db,
    AbsDatabase* chunk_path_db, AbsDatabase* file_path_db,
    size_t leaf_cache_max_bytes) {
    node_db_ = node_db;
    leaf_db_ = leaf_db;
    chunk_path_db_ = chunk_path_db;
    file_path_db_ = file_path_db;
    leaf_cache_max_bytes_ = max<size_t>(leaf_cache_max_bytes, 1);
    node_present_cache_.resize(
        static_cast<size_t>(1ULL << (BINARY_TREE_INDEX_DEPTH + 1)), 0);
    // Only store one pointer per possible leaf. CachedLeaf objects themselves
    // are allocated on admission, so empty nodes cost one contiguous pointer
    // and hot lookup becomes a direct array access.
    leaf_cache_by_position_.resize(
        static_cast<size_t>(1ULL << BINARY_TREE_INDEX_DEPTH));

    // Scan persistent metadata once at startup. Runtime updates are maintained
    // incrementally, avoiding four full DB scans after every uploaded file.
    const map<size_t, uint64_t> node_stat =
        node_db_->GetValueItemCountStat(sizeof(BinaryTreeNode_t));
    const map<size_t, uint64_t> leaf_stat =
        leaf_db_->GetValueItemCountStat(sizeof(BinaryTreeLeafEntry_t));
    const map<size_t, uint64_t> chunk_stat =
        chunk_path_db_->GetValueItemCountStat(sizeof(uint64_t));
    const map<size_t, uint64_t> file_stat =
        file_path_db_->GetValueItemCountStat(sizeof(BinaryTreePathRecord_t));
    for (const auto& item : node_stat) {
        if (item.first != 0) materialized_node_num_ += item.second;
    }
    for (const auto& item : leaf_stat) {
        if (item.first != 0) {
            leaf_bucket_num_ += item.second;
            leaf_entry_num_ += item.first * item.second;
        }
    }
    for (const auto& item : chunk_stat) {
        if (item.first != 0) indexed_chunk_num_ += item.second;
    }
    for (const auto& item : file_stat) {
        if (item.first != 0) file_path_num_ += item.second;
    }
}

BinaryTreeIndex::~BinaryTreeIndex() {
}

void BinaryTreeIndex::BuildPath(uint64_t block_tag,
    uint64_t path[BINARY_TREE_INDEX_DEPTH + 1]) const {
    uint64_t node_id = 1;
    path[0] = node_id;
    for (uint32_t level = 1; level <= BINARY_TREE_INDEX_DEPTH; level++) {
        const uint64_t bit = (block_tag >> (64 - level)) & 1ULL;
        node_id = node_id * 2 + bit;
        path[level] = node_id;
    }
}

uint64_t BinaryTreeIndex::ComputeLeafNodeID(uint64_t block_tag) const {
    // In heap numbering, a depth-D leaf is 2^D plus the D-bit path prefix.
    return (1ULL << BINARY_TREE_INDEX_DEPTH) +
        (block_tag >> (64 - BINARY_TREE_INDEX_DEPTH));
}

uint64_t BinaryTreeIndex::EnsurePath(uint64_t block_tag,
    const uint8_t* chunk_id) {
    uint64_t path[BINARY_TREE_INDEX_DEPTH + 1];
    this->BuildPath(block_tag, path);

    for (uint32_t level = 0; level <= BINARY_TREE_INDEX_DEPTH; level++) {
        if (node_present_cache_[path[level]] != 0) {
            continue;
        }
        string old_node;
        if (node_db_->QueryBuffer((char*)&path[level], sizeof(uint64_t),
            old_node)) {
            node_present_cache_[path[level]] = 1;
            continue;
        }

        BinaryTreeNode_t node = {};
        node.node_id = path[level];
        node.parent = level == 0 ? 0 : path[level - 1];
        node.level = level;
        node.position = path[level] - (1ULL << level);
        node.is_leaf = level == BINARY_TREE_INDEX_DEPTH;
        if (!node.is_leaf) {
            node.left_child = node.node_id * 2;
            node.right_child = node.node_id * 2 + 1;
        } else {
            node.has_block_tag = 1;
            node.block_tag = block_tag;
            memcpy(node.chunk_id, chunk_id, CHUNK_HASH_SIZE);
        }
        node_db_->InsertBothBuffer((char*)&node.node_id, sizeof(uint64_t),
            (char*)&node, sizeof(BinaryTreeNode_t));
        node_present_cache_[path[level]] = 1;
        materialized_node_num_++;
    }
    return path[BINARY_TREE_INDEX_DEPTH];
}

bool BinaryTreeIndex::LoadLeaf(uint64_t leaf_node_id,
    shared_ptr<vector<BinaryTreeLeafEntry_t>>& entries,
    bool record_cache_stat) {
    unique_ptr<CachedLeaf>& cache_slot =
        leaf_cache_by_position_[this->LeafCachePosition(leaf_node_id)];
    if (cache_slot != nullptr) {
        if (record_cache_stat) leaf_cache_hit_num_++;
        this->TouchCachedLeaf(leaf_node_id, *cache_slot);
        entries = cache_slot->entries;
        return true;
    }
    if (record_cache_stat) leaf_cache_miss_num_++;

    string value;
    if (!leaf_db_->QueryBuffer((char*)&leaf_node_id, sizeof(uint64_t), value)) {
        entries.reset();
        return false;
    }
    if (value.empty() || value.size() % sizeof(BinaryTreeLeafEntry_t) != 0) {
        tool::Logging(my_name_.c_str(),
            "invalid leaf value at node %lu, size=%lu.\n",
            leaf_node_id, value.size());
        entries.reset();
        return false;
    }
    const size_t entry_num = value.size() / sizeof(BinaryTreeLeafEntry_t);
    entries = make_shared<vector<BinaryTreeLeafEntry_t>>(entry_num);
    memcpy(entries->data(), value.data(), value.size());
    this->AdmitLeafToCache(leaf_node_id, entries);
    return true;
}

void BinaryTreeIndex::BatchLoadLeaf(const vector<uint64_t>& leaf_node_ids,
    vector<shared_ptr<vector<BinaryTreeLeafEntry_t>>>& entries,
    vector<bool>& found, bool record_cache_stat) {
    entries.assign(leaf_node_ids.size(), nullptr);
    found.assign(leaf_node_ids.size(), false);

    vector<string> miss_keys;
    vector<size_t> miss_positions;
    miss_keys.reserve(leaf_node_ids.size());
    miss_positions.reserve(leaf_node_ids.size());

    for (size_t leaf_idx = 0; leaf_idx < leaf_node_ids.size(); leaf_idx++) {
        const uint64_t leaf_node_id = leaf_node_ids[leaf_idx];
        unique_ptr<CachedLeaf>& cache_slot = leaf_cache_by_position_[
            this->LeafCachePosition(leaf_node_id)];
        if (cache_slot != nullptr) {
            if (record_cache_stat) leaf_cache_hit_num_++;
            this->TouchCachedLeaf(leaf_node_id, *cache_slot);
            entries[leaf_idx] = cache_slot->entries;
            found[leaf_idx] = true;
            continue;
        }

        if (record_cache_stat) leaf_cache_miss_num_++;
        miss_keys.emplace_back(reinterpret_cast<const char*>(&leaf_node_id),
            sizeof(uint64_t));
        miss_positions.push_back(leaf_idx);
    }

    if (miss_keys.empty()) {
        return;
    }

    vector<string> miss_values;
    const vector<bool> miss_found = leaf_db_->BatchQueryBuffer(miss_keys,
        miss_values);
    for (size_t miss_idx = 0; miss_idx < miss_positions.size(); miss_idx++) {
        if (miss_idx >= miss_found.size() || !miss_found[miss_idx] ||
            miss_idx >= miss_values.size()) {
            continue;
        }

        const size_t result_idx = miss_positions[miss_idx];
        const uint64_t leaf_node_id = leaf_node_ids[result_idx];
        const string& value = miss_values[miss_idx];
        if (value.empty() ||
            value.size() % sizeof(BinaryTreeLeafEntry_t) != 0) {
            tool::Logging(my_name_.c_str(),
                "invalid leaf value at node %lu, size=%lu.\n",
                leaf_node_id, value.size());
            continue;
        }

        const size_t entry_num = value.size() /
            sizeof(BinaryTreeLeafEntry_t);
        entries[result_idx] =
            make_shared<vector<BinaryTreeLeafEntry_t>>(entry_num);
        memcpy(entries[result_idx]->data(), value.data(), value.size());
        found[result_idx] = true;
        this->AdmitLeafToCache(leaf_node_id, entries[result_idx]);
    }
}

size_t BinaryTreeIndex::EstimateLeafCacheBytes(
    const vector<BinaryTreeLeafEntry_t>& entries) const {
    return sizeof(vector<BinaryTreeLeafEntry_t>) +
        entries.capacity() * sizeof(BinaryTreeLeafEntry_t);
}

size_t BinaryTreeIndex::LeafCachePosition(uint64_t leaf_node_id) const {
    return static_cast<size_t>(leaf_node_id -
        (1ULL << BINARY_TREE_INDEX_DEPTH));
}

void BinaryTreeIndex::TouchCachedLeaf(uint64_t leaf_node_id,
    CachedLeaf& cached_leaf) {
    leaf_cache_lru_.erase(cached_leaf.lru_position);
    leaf_cache_lru_.push_front(leaf_node_id);
    cached_leaf.lru_position = leaf_cache_lru_.begin();
}

void BinaryTreeIndex::AdmitLeafToCache(uint64_t leaf_node_id,
    const shared_ptr<vector<BinaryTreeLeafEntry_t>>& entries) {
    const size_t new_bytes = this->EstimateLeafCacheBytes(*entries);
    unique_ptr<CachedLeaf>& cache_slot =
        leaf_cache_by_position_[this->LeafCachePosition(leaf_node_id)];
    if (cache_slot != nullptr) {
        leaf_cache_current_bytes_ -= cache_slot->accounted_bytes;
        cache_slot->entries = entries;
        cache_slot->accounted_bytes = new_bytes;
        leaf_cache_current_bytes_ += new_bytes;
        this->TouchCachedLeaf(leaf_node_id, *cache_slot);
    } else {
        leaf_cache_lru_.push_front(leaf_node_id);
        cache_slot = make_unique<CachedLeaf>(CachedLeaf{
            entries, new_bytes, leaf_cache_lru_.begin()});
        leaf_cache_entry_num_++;
        leaf_cache_current_bytes_ += new_bytes;
    }

    leaf_cache_peak_bytes_ = max(leaf_cache_peak_bytes_,
        leaf_cache_current_bytes_);
    this->EnforceLeafCacheLimit();
}

void BinaryTreeIndex::EnforceLeafCacheLimit() {
    while (leaf_cache_current_bytes_ >
        leaf_cache_max_bytes_ && !leaf_cache_lru_.empty()) {
        const uint64_t victim_id = leaf_cache_lru_.back();
        unique_ptr<CachedLeaf>& victim =
            leaf_cache_by_position_[this->LeafCachePosition(victim_id)];
        if (victim == nullptr) {
            leaf_cache_lru_.pop_back();
            continue;
        }
        leaf_cache_current_bytes_ -= victim->accounted_bytes;
        leaf_cache_lru_.pop_back();
        victim.reset();
        leaf_cache_entry_num_--;
        leaf_cache_eviction_num_++;
    }
}

bool BinaryTreeIndex::IsUsableBase(
    const KeyForChunkHashDB_t& location) const {
    // The current download format can directly read full chunks only. Delta
    // chunks remain indexed for exact sharing/path management but are not
    // presented as bases until recursive recovery is implemented.
    return location.stat == COMP_BASE_CHUNK ||
        location.stat == UNCOMP_BASE_CHUNK;
}

bool BinaryTreeIndex::FindChunk(const uint8_t* chunk_id,
    uint64_t* leaf_node_id) {
    lock_guard<mutex> lock(index_mutex_);
    string value;
    if (!chunk_path_db_->QueryBuffer((char*)chunk_id, CHUNK_HASH_SIZE, value) ||
        value.size() < sizeof(uint64_t) || value.size() % sizeof(uint64_t) != 0) {
        exact_miss_num_++;
        return false;
    }
    if (leaf_node_id != nullptr) {
        memcpy(leaf_node_id, value.data(), sizeof(uint64_t));
    }
    exact_hit_num_++;
    return true;
}

void BinaryTreeIndex::FindBaseCandidates(const uint64_t* features,
    const uint8_t* current_chunk_id,
    vector<BinaryTreeCandidate>& candidates) {
    const auto search_start = chrono::steady_clock::now();
    lock_guard<mutex> lock(index_mutex_);
    candidates.clear();
    candidate_search_num_++;
    unordered_set<uint64_t> seen_leaves;
    size_t searched_path_num = 0;
    struct LoadedLeaf {
        uint64_t leaf_node_id;
        shared_ptr<const vector<BinaryTreeLeafEntry_t>> entries;
    };
    vector<LoadedLeaf> loaded_leaves;
    loaded_leaves.reserve(MAX_TREE_PATHS);
    vector<RankedTreeCandidate> merged_candidates;
    unordered_map<string, size_t> merged_candidate_positions;
    vector<uint64_t> leaf_node_ids;
    leaf_node_ids.reserve(MAX_TREE_PATHS);

    // Collect unique leaf IDs first. Preserve path order so the candidate
    // merge and ranking stages receive leaves in exactly the previous order.
    for (size_t feature_idx = 0; feature_idx < SUPER_FEATURE_PER_CHUNK;
        feature_idx++) {
        const uint64_t leaf_node_id =
            this->ComputeLeafNodeID(features[feature_idx]);
        if (!seen_leaves.insert(leaf_node_id).second) {
            continue;
        }
        if (searched_path_num >= MAX_TREE_PATHS) {
            break;
        }
        searched_path_num++;
        tree_path_search_num_++;
        leaf_node_ids.push_back(leaf_node_id);
    }

    // Resolve cache hits directly, then issue one backend batch read for all
    // cache misses instead of one QueryBuffer call per path.
    vector<shared_ptr<vector<BinaryTreeLeafEntry_t>>> batch_entries;
    vector<bool> batch_found;
    this->BatchLoadLeaf(leaf_node_ids, batch_entries, batch_found, true);
    for (size_t leaf_idx = 0; leaf_idx < leaf_node_ids.size(); leaf_idx++) {
        if (!batch_found[leaf_idx]) {
            continue;
        }
        candidate_leaf_num_++;
        loaded_leaf_entry_num_.fetch_add(batch_entries[leaf_idx]->size(),
            memory_order_relaxed);
        UpdateAtomicMaximum(max_loaded_leaf_size_,
            batch_entries[leaf_idx]->size());
        loaded_leaves.push_back({leaf_node_ids[leaf_idx],
            batch_entries[leaf_idx]});
    }
    const auto filter_start = chrono::steady_clock::now();
    binary_tree_search_time_us_.fetch_add(
        chrono::duration_cast<chrono::microseconds>(
            filter_start - search_start).count(), memory_order_relaxed);

    for (const auto& loaded_leaf : loaded_leaves) {
        // Maintain a bounded Top-K while scanning. This avoids allocating and
        // sorting one RankedTreeCandidate for every entry in a large leaf.
        vector<RankedTreeCandidate> leaf_candidates;
        leaf_candidates.reserve(MAX_CANDIDATES_PER_LEAF);
        for (const auto& entry : *loaded_leaf.entries) {
            if (memcmp(entry.chunk_id, current_chunk_id, CHUNK_HASH_SIZE) == 0 ||
                !this->IsUsableBase(entry.storage_location)) {
                continue;
            }
            RankedTreeCandidate ranked = RankTreeCandidate(features,
                loaded_leaf.leaf_node_id, entry);
            // This is a safe tree-local prune: SimilarPolicy would reject the
            // same candidate because it uses the identical match threshold.
            if (ranked.exact_feature_matches <
                CIPHER_FEATURE_MATCH_THRESHOLD) {
                continue;
            }
            KeepBestLeafCandidate(leaf_candidates, std::move(ranked));
        }

        sort(leaf_candidates.begin(), leaf_candidates.end(),
            BetterTreeCandidate);
        for (size_t rank_idx = 0; rank_idx < leaf_candidates.size();
            rank_idx++) {
            const RankedTreeCandidate& ranked = leaf_candidates[rank_idx];
            const string candidate_id((char*)ranked.candidate.chunk_id,
                CHUNK_HASH_SIZE);
            auto position = merged_candidate_positions.find(candidate_id);
            if (position == merged_candidate_positions.end()) {
                merged_candidate_positions[candidate_id] =
                    merged_candidates.size();
                merged_candidates.push_back(ranked);
            } else if (BetterTreeCandidate(ranked,
                merged_candidates[position->second])) {
                merged_candidates[position->second] = ranked;
            }
        }
    }

    const size_t total_top_k = min(merged_candidates.size(),
        MAX_BINARY_TREE_CANDIDATES_PER_CHUNK);
    partial_sort(merged_candidates.begin(),
        merged_candidates.begin() + total_top_k, merged_candidates.end(),
        BetterTreeCandidate);
    candidates.reserve(total_top_k);
    for (size_t rank_idx = 0; rank_idx < total_top_k; rank_idx++) {
        candidates.push_back(merged_candidates[rank_idx].candidate);
    }
    // "Verified" means the candidates that actually leave the tree and enter
    // SimilarPolicy, rather than every raw entry scanned from a leaf.
    candidate_verify_num_.fetch_add(candidates.size(), memory_order_relaxed);
    candidate_returned_num_.fetch_add(candidates.size(), memory_order_relaxed);
    UpdateAtomicMaximum(max_candidate_num_, candidates.size());
    candidate_filtering_time_us_.fetch_add(
        chrono::duration_cast<chrono::microseconds>(
            chrono::steady_clock::now() - filter_start).count(),
        memory_order_relaxed);
}

bool BinaryTreeIndex::InsertChunk(const ChunkInfo_t* info) {
    lock_guard<mutex> lock(index_mutex_);
    vector<uint64_t> leaf_ids;
    leaf_ids.reserve(SUPER_FEATURE_PER_CHUNK);

    for (size_t feature_idx = 0; feature_idx < SUPER_FEATURE_PER_CHUNK;
        feature_idx++) {
        const uint64_t leaf_node_id = this->EnsurePath(
            info->features[feature_idx], info->fp);
        if (find(leaf_ids.begin(), leaf_ids.end(), leaf_node_id) ==
            leaf_ids.end()) {
            leaf_ids.push_back(leaf_node_id);
        }

        shared_ptr<vector<BinaryTreeLeafEntry_t>> entries;
        const bool leaf_existed = this->LoadLeaf(leaf_node_id, entries, false);
        if (!leaf_existed) {
            entries = make_shared<vector<BinaryTreeLeafEntry_t>>();
            leaf_bucket_num_++;
        }
        auto existing = find_if(entries->begin(), entries->end(),
            [info](const BinaryTreeLeafEntry_t& entry) {
                return memcmp(entry.chunk_id, info->fp,
                    CHUNK_HASH_SIZE) == 0;
            });

        BinaryTreeLeafEntry_t new_entry = {};
        new_entry.block_tag = info->features[feature_idx];
        memcpy(new_entry.chunk_id, info->fp, CHUNK_HASH_SIZE);
        memcpy(new_entry.features, info->features,
            sizeof(uint64_t) * SUPER_FEATURE_PER_CHUNK);
        new_entry.storage_location = info->addr;
        if (existing == entries->end()) {
            entries->push_back(new_entry);
            leaf_entry_num_++;
        } else {
            *existing = new_entry;
        }

        leaf_db_->InsertBothBuffer((char*)&leaf_node_id, sizeof(uint64_t),
            (char*)entries->data(),
            entries->size() * sizeof(BinaryTreeLeafEntry_t));
        // The database write above is authoritative. Refresh or admit the
        // modified leaf, then evict least-recently-used leaves if over budget.
        this->AdmitLeafToCache(leaf_node_id, entries);
    }

    string existing_path;
    const bool already_indexed = chunk_path_db_->QueryBuffer(
        (char*)info->fp, CHUNK_HASH_SIZE, existing_path);
    const bool inserted = chunk_path_db_->InsertBothBuffer((char*)info->fp,
        CHUNK_HASH_SIZE, (char*)leaf_ids.data(),
        leaf_ids.size() * sizeof(uint64_t));
    if (inserted && !already_indexed) indexed_chunk_num_++;
    return inserted;
}

bool BinaryTreeIndex::RecordFileRecipe(const string& file_tag,
    const string& recipe_path) {
    ifstream recipe(recipe_path, ios_base::binary | ios_base::in);
    if (!recipe.is_open()) {
        tool::Logging(my_name_.c_str(), "cannot open recipe %s.\n",
            recipe_path.c_str());
        return false;
    }

    FileRecipeHead_t header = {};
    recipe.read((char*)&header, sizeof(FileRecipeHead_t));
    if (!recipe.good()) {
        return false;
    }

    lock_guard<mutex> lock(index_mutex_);
    for (uint64_t chunk_idx = 0; chunk_idx < header.chunk_num; chunk_idx++) {
        uint8_t chunk_id[CHUNK_HASH_SIZE];
        recipe.read((char*)chunk_id, CHUNK_HASH_SIZE);
        if (!recipe.good()) {
            tool::Logging(my_name_.c_str(),
                "truncated recipe %s at chunk %lu.\n",
                recipe_path.c_str(), chunk_idx);
            return false;
        }

        string leaf_value;
        if (!chunk_path_db_->QueryBuffer((char*)chunk_id, CHUNK_HASH_SIZE,
            leaf_value) || leaf_value.size() % sizeof(uint64_t) != 0) {
            // Old chunks that predate the tree have no path yet.
            continue;
        }

        const size_t leaf_num = leaf_value.size() / sizeof(uint64_t);
        for (size_t leaf_idx = 0; leaf_idx < leaf_num; leaf_idx++) {
            uint64_t leaf_node_id;
            memcpy(&leaf_node_id, leaf_value.data() +
                leaf_idx * sizeof(uint64_t), sizeof(uint64_t));

            BinaryTreePathRecord_t record = {};
            memcpy(record.chunk_id, chunk_id, CHUNK_HASH_SIZE);
            record.leaf_node_id = leaf_node_id;
            // Reconstruct the exact root-to-leaf path from the heap node ID.
            record.path[BINARY_TREE_INDEX_DEPTH] = leaf_node_id;
            for (uint32_t level = BINARY_TREE_INDEX_DEPTH; level > 0;
                level--) {
                record.path[level - 1] = record.path[level] / 2;
            }

            string key = file_tag;
            key.append((char*)&chunk_idx, sizeof(uint64_t));
            const uint32_t leaf_ordinal = static_cast<uint32_t>(leaf_idx);
            key.append((char*)&leaf_ordinal, sizeof(uint32_t));
            string previous_record;
            const bool existed = file_path_db_->Query(key, previous_record);
            if (file_path_db_->InsertBuffer(key, (char*)&record,
                sizeof(BinaryTreePathRecord_t)) && !existed) {
                file_path_num_++;
            }
        }
    }
    return true;
}

BinaryTreeIndexStat BinaryTreeIndex::GetStat() const {
    lock_guard<mutex> lock(index_mutex_);
    BinaryTreeIndexStat stat;
    stat.materialized_node_num = materialized_node_num_.load();
    stat.leaf_bucket_num = leaf_bucket_num_.load();
    stat.leaf_entry_num = leaf_entry_num_.load();
    stat.indexed_chunk_num = indexed_chunk_num_.load();
    stat.file_path_num = file_path_num_.load();
    stat.exact_hit_num = exact_hit_num_.load();
    stat.exact_miss_num = exact_miss_num_.load();
    stat.candidate_search_num = candidate_search_num_.load();
    stat.candidate_leaf_num = candidate_leaf_num_.load();
    stat.candidate_verify_num = candidate_verify_num_.load();
    stat.candidate_returned_num = candidate_returned_num_.load();
    stat.max_candidate_num = max_candidate_num_.load();
    stat.loaded_leaf_entry_num = loaded_leaf_entry_num_.load();
    stat.max_loaded_leaf_size = max_loaded_leaf_size_.load();
    stat.tree_path_search_num = tree_path_search_num_.load();
    stat.leaf_cache_hit_num = leaf_cache_hit_num_.load();
    stat.leaf_cache_miss_num = leaf_cache_miss_num_.load();
    stat.binary_tree_search_time_us = binary_tree_search_time_us_.load();
    stat.candidate_filtering_time_us = candidate_filtering_time_us_.load();
    stat.leaf_cache_entry_num = leaf_cache_entry_num_;
    stat.leaf_cache_current_bytes = leaf_cache_current_bytes_;
    stat.leaf_cache_peak_bytes = leaf_cache_peak_bytes_;
    stat.leaf_cache_limit_bytes = leaf_cache_max_bytes_;
    stat.leaf_cache_eviction_num = leaf_cache_eviction_num_;
    return stat;
}
