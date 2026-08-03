/**
 * @file inform_cache.cc
 * @author Zuoru YANG (zryang@cse.cuhk.edu.hk)
 * @brief implement the global unified InformCache (shared across all clients)
 * @version 0.2
 * @date 2022-08-02
 *
 * @copyright Copyright (c) 2022
 *
 */

#include "../../include/server/inform_cache.h"

// ========== 静态成员定义 ==========
unordered_map<uint64_t, vector<string>> InformCache::unified_feature_2_fp_db_;
unordered_map<string, pair<uint32_t, uint32_t>> InformCache::unified_base_2_cnt_idx_;
unordered_map<uint32_t, unordered_map<uint64_t, unordered_map<string, bool>>>
    InformCache::client_feature_2_fp_db_;
unordered_map<uint64_t, unordered_map<string, uint32_t>>
    InformCache::feature_base_ref_cnt_;
unordered_map<uint64_t, unordered_map<string, uint32_t>>
    InformCache::persisted_feature_base_ref_cnt_;
AbsDatabase* InformCache::unified_base_2_data_db_ = nullptr;
AbsDatabase* InformCache::global_feature_2_fp_db_ = nullptr;
boost::mutex InformCache::cache_lock_;
string InformCache::cache_root_path_;
string InformCache::cache_base_chunk_str_;
uint64_t InformCache::local_candidate_lookup_chunk_num_ = 0;
uint64_t InformCache::local_zero_candidate_chunk_num_ = 0;
uint64_t InformCache::local_single_candidate_chunk_num_ = 0;
uint64_t InformCache::local_multi_candidate_chunk_num_ = 0;
uint64_t InformCache::local_total_candidate_num_ = 0;
uint64_t InformCache::local_max_candidate_num_ = 0;
uint64_t InformCache::local_live_candidate_num_ = 0;
uint64_t InformCache::local_stale_candidate_num_ = 0;
uint64_t InformCache::local_missing_data_candidate_num_ = 0;
uint64_t InformCache::local_delta_evaluation_num_ = 0;
uint64_t InformCache::local_selected_not_best_num_ = 0;
uint64_t InformCache::local_potential_saved_bytes_ = 0;
uint64_t InformCache::local_missed_live_best_num_ = 0;
uint64_t InformCache::local_selected_below_threshold_num_ = 0;
uint64_t InformCache::local_best_avoids_reject_num_ = 0;
uint64_t InformCache::local_bucket_eviction_num_ = 0;

// ========== 全局生命周期管理 ==========

/**
 * @brief initialize the global shared cache (called once in server_main)
 *
 * @param feature_2_fp_db the global LevelDB for feature-to-fp persistence
 */
void InformCache::InitGlobalCache(AbsDatabase* feature_2_fp_db) {
    global_feature_2_fp_db_ = feature_2_fp_db;
    cache_root_path_ = config.GetCacheRootPath();

    DatabaseFactory db_factory;
    string db_path = cache_root_path_ + "unified_db";
    unified_base_2_data_db_ = db_factory.CreateDatabase(ROCKSDB_DB, db_path);

    LoadCntIdx();
    if (!LoadAssociations()) {
        // Legacy metadata only stored an aggregate count. It cannot support
        // exact per-client eviction, so do not treat those counts as live
        // index references. Existing deltas remain recoverable through their
        // compressed copies in the storage pool.
        for (auto& item : unified_base_2_cnt_idx_) {
            item.second.first = 0;
        }
        tool::Logging("InformCache", "legacy cache metadata detected; "
            "base associations will be rebuilt and stale bases reclaimed.\n");
    }
    cache_base_chunk_str_.reserve(ENC_MAX_CHUNK_SIZE);

    tool::Logging("InformCache", "global cache initialized, unified_db at: %s\n",
        db_path.c_str());
}

/**
 * @brief destroy the global shared cache (called once in server_main shutdown)
 */
void InformCache::DestroyGlobalCache() {
    boost::mutex::scoped_lock lock(cache_lock_);

    // 清理引用计数为0的base块
    uint64_t total_remaining = 0;
    auto it = unified_base_2_cnt_idx_.begin();
    while (it != unified_base_2_cnt_idx_.end()) {
        if (it->second.first == 0) {
            unified_base_2_data_db_->Delete(it->first);
            it = unified_base_2_cnt_idx_.erase(it);
        } else {
            total_remaining += it->second.second;
            it++;
        }
    }

    StoreCntIdx();
    StoreAssociations();
    ReportLocalCandidateStats();

    delete unified_base_2_data_db_;
    unified_base_2_data_db_ = nullptr;

    unified_feature_2_fp_db_.clear();
    client_feature_2_fp_db_.clear();
    feature_base_ref_cnt_.clear();
    persisted_feature_base_ref_cnt_.clear();

    tool::Logging("InformCache", "global cache destroyed, remaining size: %lu\n",
        total_remaining);

    SimilarPolicy::ReportEvictStats();
}

// ========== 构造 / 析构（轻量级无状态工具） ==========

/**
 * @brief Construct a new InformCache object (lightweight, stateless utilities only)
 */
InformCache::InformCache() {
    finesse_util_ = new FinesseUtil(SUPER_FEATURE_PER_CHUNK,
        FEATURE_PER_CHUNK, FEATURE_PER_SUPER_FEATURE);
    rabin_util_ = new RabinFPUtil(config.GetSimilarSlidingWinSize());
    rabin_util_->NewCtx(rabin_ctx_);
    similar_policy_ = new SimilarPolicy();
    delta_comp_ = new DeltaComp();
}

/**
 * @brief Destroy the InformCache object
 */
InformCache::~InformCache() {
    delete finesse_util_;
    rabin_util_->FreeCtx(rabin_ctx_);
    delete similar_policy_;
    delete delta_comp_;
    delete rabin_util_;
}

bool InformCache::IsBaseLive(const string& base_fp) {
    auto it = unified_base_2_cnt_idx_.find(base_fp);
    return it != unified_base_2_cnt_idx_.end() && it->second.first > 0;
}

InformCache::CandidateDiagnostic InformCache::MeasureLocalCandidates(
    WrappedChunk_t* input_chunk) {
    CandidateDiagnostic diagnostic;
    vector<string> candidates;
    vector<string> persisted_candidates;

    similar_policy_->GetBaseCandidates(unified_feature_2_fp_db_,
        input_chunk->info.features, candidates);
    similar_policy_->GetBaseCandidates(global_feature_2_fp_db_,
        input_chunk->info.features, persisted_candidates);
    for (const auto& candidate : persisted_candidates) {
        if (find(candidates.begin(), candidates.end(), candidate) ==
            candidates.end()) {
            candidates.push_back(candidate);
        }
    }

    local_candidate_lookup_chunk_num_++;
    local_total_candidate_num_ += candidates.size();
    local_max_candidate_num_ = max(local_max_candidate_num_,
        (uint64_t)candidates.size());
    if (candidates.empty()) local_zero_candidate_chunk_num_++;
    else if (candidates.size() == 1) local_single_candidate_chunk_num_++;
    else local_multi_candidate_chunk_num_++;

    uint8_t delta_buf[ENC_MAX_CHUNK_SIZE];
    for (const auto& candidate : candidates) {
        if (!IsBaseLive(candidate)) {
            local_stale_candidate_num_++;
            continue;
        }

        string base_chunk;
        if (!unified_base_2_data_db_->Query(candidate, base_chunk)) {
            local_missing_data_candidate_num_++;
            continue;
        }
        local_live_candidate_num_++;
        uint32_t delta_size = delta_comp_->DeltaEncode(
            (uint8_t*)base_chunk.data(), base_chunk.size(), input_chunk->data,
            input_chunk->info.size, delta_buf);
        local_delta_evaluation_num_++;
        if (delta_size > 0 && (diagnostic.best_base_fp.empty() ||
            delta_size < diagnostic.best_delta_size)) {
            diagnostic.best_base_fp = candidate;
            diagnostic.best_delta_size = delta_size;
            diagnostic.best_delta.assign(delta_buf, delta_buf + delta_size);
        }
    }
    return diagnostic;
}

void InformCache::RecordSelectedCandidate(
    const CandidateDiagnostic& diagnostic, const string& selected_base_fp,
    uint32_t selected_delta_size, uint32_t input_size) {
    if (selected_delta_size == 0 || diagnostic.best_base_fp.empty()) return;

    if (diagnostic.best_base_fp != selected_base_fp &&
        diagnostic.best_delta_size < selected_delta_size) {
        local_selected_not_best_num_++;
        local_potential_saved_bytes_ += selected_delta_size -
            diagnostic.best_delta_size;
    }

    double selected_similarity = 1.0 - (double)selected_delta_size /
        (double)input_size;
    if (selected_similarity < SIMILARITY_THRESHOLD) {
        local_selected_below_threshold_num_++;
        double best_similarity = 1.0 - (double)diagnostic.best_delta_size /
            (double)input_size;
        if (best_similarity >= SIMILARITY_THRESHOLD) {
            local_best_avoids_reject_num_++;
        }
    }
}

void InformCache::RecordMissedCandidate(
    const CandidateDiagnostic& diagnostic) {
    if (!diagnostic.best_base_fp.empty()) local_missed_live_best_num_++;
}

void InformCache::ReportLocalCandidateStats() {
    uint64_t bucket_hist[MAX_BASE_PER_FEATURE + 1] = {0};
    for (const auto& item : unified_feature_2_fp_db_) {
        size_t bucket_size = min(item.second.size(),
            (size_t)MAX_BASE_PER_FEATURE);
        bucket_hist[bucket_size]++;
    }

    fprintf(stderr, "\n========== Local Candidate Diagnostic ==========\n");
    fprintf(stderr, "lookup chunks:                 %lu\n",
        local_candidate_lookup_chunk_num_);
    fprintf(stderr, "zero / single / multi chunks:  %lu / %lu / %lu\n",
        local_zero_candidate_chunk_num_, local_single_candidate_chunk_num_,
        local_multi_candidate_chunk_num_);
    fprintf(stderr, "candidate total / max:         %lu / %lu\n",
        local_total_candidate_num_, local_max_candidate_num_);
    fprintf(stderr, "live / stale / missing data:   %lu / %lu / %lu\n",
        local_live_candidate_num_, local_stale_candidate_num_,
        local_missing_data_candidate_num_);
    fprintf(stderr, "delta evaluations:             %lu\n",
        local_delta_evaluation_num_);
    fprintf(stderr, "selected not best:             %lu\n",
        local_selected_not_best_num_);
    fprintf(stderr, "potential delta bytes saved:   %lu\n",
        local_potential_saved_bytes_);
    fprintf(stderr, "missed live best candidate:    %lu\n",
        local_missed_live_best_num_);
    fprintf(stderr, "selected below threshold:      %lu\n",
        local_selected_below_threshold_num_);
    fprintf(stderr, "best candidate avoids reject:  %lu\n",
        local_best_avoids_reject_num_);
    fprintf(stderr, "local bucket FIFO evictions:   %lu\n",
        local_bucket_eviction_num_);
    fprintf(stderr, "final bucket occupancy:");
    for (size_t i = 0; i <= MAX_BASE_PER_FEATURE; i++) {
        fprintf(stderr, " %lu:%lu", i, bucket_hist[i]);
    }
    fprintf(stderr, "\n================================================\n\n");

    ofstream stat_hdl("local-candidate-stat", ios_base::trunc |
        ios_base::out);
    if (!stat_hdl.is_open()) {
        tool::Logging("InformCache", "cannot write local candidate stats.\n");
        return;
    }
    stat_hdl << "metric,value\n"
        << "lookup_chunks," << local_candidate_lookup_chunk_num_ << "\n"
        << "zero_candidate_chunks," << local_zero_candidate_chunk_num_ << "\n"
        << "single_candidate_chunks," << local_single_candidate_chunk_num_ << "\n"
        << "multi_candidate_chunks," << local_multi_candidate_chunk_num_ << "\n"
        << "candidate_total," << local_total_candidate_num_ << "\n"
        << "candidate_max," << local_max_candidate_num_ << "\n"
        << "live_candidates," << local_live_candidate_num_ << "\n"
        << "stale_candidates," << local_stale_candidate_num_ << "\n"
        << "missing_data_candidates," << local_missing_data_candidate_num_ << "\n"
        << "delta_evaluations," << local_delta_evaluation_num_ << "\n"
        << "selected_not_best," << local_selected_not_best_num_ << "\n"
        << "potential_delta_bytes_saved," << local_potential_saved_bytes_ << "\n"
        << "missed_live_best_candidate," << local_missed_live_best_num_ << "\n"
        << "selected_below_threshold," <<
            local_selected_below_threshold_num_ << "\n"
        << "best_candidate_avoids_reject," <<
            local_best_avoids_reject_num_ << "\n"
        << "local_bucket_fifo_evictions," << local_bucket_eviction_num_ << "\n";
    for (size_t i = 0; i <= MAX_BASE_PER_FEATURE; i++) {
        stat_hdl << "final_bucket_size_" << i << "," << bucket_hist[i] << "\n";
    }
    stat_hdl.close();
}

void InformCache::RemoveBaseIfUnused(const string& base_fp) {
    auto it = unified_base_2_cnt_idx_.find(base_fp);
    if (it == unified_base_2_cnt_idx_.end() || it->second.first != 0) {
        return;
    }
    unified_base_2_data_db_->Delete(base_fp);
    unified_base_2_cnt_idx_.erase(it);
}

void InformCache::RemoveAssociationForAllClients(uint64_t feature,
    const string& base_fp) {
    for (auto& client_entry : client_feature_2_fp_db_) {
        auto feature_it = client_entry.second.find(feature);
        if (feature_it == client_entry.second.end()) {
            continue;
        }
        auto base_it = feature_it->second.find(base_fp);
        if (base_it == feature_it->second.end()) {
            continue;
        }

        auto meta_it = unified_base_2_cnt_idx_.find(base_fp);
        if (meta_it != unified_base_2_cnt_idx_.end() &&
            meta_it->second.first > 0) {
            meta_it->second.first--;
        }
        feature_it->second.erase(base_it);
        if (feature_it->second.empty()) {
            client_entry.second.erase(feature_it);
        }
    }

    auto global_it = unified_feature_2_fp_db_.find(feature);
    if (global_it != unified_feature_2_fp_db_.end()) {
        auto& candidates = global_it->second;
        candidates.erase(remove(candidates.begin(), candidates.end(), base_fp),
            candidates.end());
        if (candidates.empty()) {
            unified_feature_2_fp_db_.erase(global_it);
        }
    }
    auto ref_it = feature_base_ref_cnt_.find(feature);
    if (ref_it != feature_base_ref_cnt_.end()) {
        ref_it->second.erase(base_fp);
        if (ref_it->second.empty()) feature_base_ref_cnt_.erase(ref_it);
    }
    auto persisted_it = persisted_feature_base_ref_cnt_.find(feature);
    if (persisted_it != persisted_feature_base_ref_cnt_.end()) {
        persisted_it->second.erase(base_fp);
        if (persisted_it->second.empty()) {
            persisted_feature_base_ref_cnt_.erase(persisted_it);
        }
    }
    similar_policy_->RemoveFeatureIndex(global_feature_2_fp_db_, feature,
        base_fp);
    RemoveBaseIfUnused(base_fp);
}

void InformCache::AddAssociation(uint32_t client_id, uint64_t feature,
    const string& base_fp, bool persist) {
    auto client_it = client_feature_2_fp_db_.find(client_id);
    if (client_it != client_feature_2_fp_db_.end()) {
        auto feature_it = client_it->second.find(feature);
        if (feature_it != client_it->second.end()) {
            auto base_it = feature_it->second.find(base_fp);
            if (base_it != feature_it->second.end()) {
                if (persist && !base_it->second) {
                    base_it->second = true;
                    persisted_feature_base_ref_cnt_[feature][base_fp]++;
                    similar_policy_->UpdateFeatureIndex(global_feature_2_fp_db_,
                        feature, base_fp);
                }
                return;
            }
        }
    }

    auto& candidates = unified_feature_2_fp_db_[feature];
    if (find(candidates.begin(), candidates.end(), base_fp) ==
        candidates.end()) {
        if (candidates.size() >= MAX_BASE_PER_FEATURE) {
            string evicted_fp = candidates.front();
            RemoveAssociationForAllClients(feature, evicted_fp);
            local_bucket_eviction_num_++;
            SimilarPolicy::evict_total_calls_++;
        }
        unified_feature_2_fp_db_[feature].push_back(base_fp);
    }

    client_feature_2_fp_db_[client_id][feature][base_fp] = persist;
    feature_base_ref_cnt_[feature][base_fp]++;
    if (persist) {
        persisted_feature_base_ref_cnt_[feature][base_fp]++;
        similar_policy_->UpdateFeatureIndex(global_feature_2_fp_db_, feature,
            base_fp);
    }

    auto meta_it = unified_base_2_cnt_idx_.find(base_fp);
    if (meta_it != unified_base_2_cnt_idx_.end()) {
        meta_it->second.first++;
    }
}

void InformCache::RemoveClientFeature(uint32_t client_id, uint64_t feature) {
    auto client_it = client_feature_2_fp_db_.find(client_id);
    if (client_it == client_feature_2_fp_db_.end()) {
        return;
    }
    auto feature_it = client_it->second.find(feature);
    if (feature_it == client_it->second.end()) {
        return;
    }

    vector<pair<string, bool>> associations(feature_it->second.begin(),
        feature_it->second.end());
    client_it->second.erase(feature_it);
    if (client_it->second.empty()) {
        client_feature_2_fp_db_.erase(client_it);
    }

    for (const auto& association : associations) {
        const string& base_fp = association.first;
        bool persisted = association.second;

        auto meta_it = unified_base_2_cnt_idx_.find(base_fp);
        if (meta_it != unified_base_2_cnt_idx_.end() &&
            meta_it->second.first > 0) {
            meta_it->second.first--;
        }

        auto ref_it = feature_base_ref_cnt_.find(feature);
        if (ref_it != feature_base_ref_cnt_.end()) {
            auto base_ref_it = ref_it->second.find(base_fp);
            if (base_ref_it != ref_it->second.end() &&
                base_ref_it->second > 0 && --base_ref_it->second == 0) {
                ref_it->second.erase(base_ref_it);
                auto global_it = unified_feature_2_fp_db_.find(feature);
                if (global_it != unified_feature_2_fp_db_.end()) {
                    auto& candidates = global_it->second;
                    candidates.erase(remove(candidates.begin(), candidates.end(),
                        base_fp), candidates.end());
                    if (candidates.empty()) {
                        unified_feature_2_fp_db_.erase(global_it);
                    }
                }
            }
            if (ref_it->second.empty()) feature_base_ref_cnt_.erase(ref_it);
        }

        if (persisted) {
            auto persisted_it = persisted_feature_base_ref_cnt_.find(feature);
            if (persisted_it != persisted_feature_base_ref_cnt_.end()) {
                auto base_ref_it = persisted_it->second.find(base_fp);
                if (base_ref_it != persisted_it->second.end() &&
                    base_ref_it->second > 0 && --base_ref_it->second == 0) {
                    persisted_it->second.erase(base_ref_it);
                    similar_policy_->RemoveFeatureIndex(global_feature_2_fp_db_,
                        feature, base_fp);
                }
                if (persisted_it->second.empty()) {
                    persisted_feature_base_ref_cnt_.erase(persisted_it);
                }
            }
        }
        RemoveBaseIfUnused(base_fp);
    }
}

// ========== 上传路径 ==========

/**
 * @brief process a normal chunk with two-level similarity lookup
 *
 * Level 1: unified_feature_2_fp_db_ (in-memory, fast)
 * Level 2: global_feature_2_fp_db_ (LevelDB, comprehensive fallback)
 *
 * @param input_chunk input chunk
 * @param output_chunk output chunk
 * @return true delta compression performed
 * @return false cannot perform delta compression
 */
bool InformCache::ProcessNormalChunk(WrappedChunk_t* input_chunk,
    WrappedChunk_t* output_chunk, uint32_t client_id) {
    boost::mutex::scoped_lock lock(cache_lock_);
    CandidateDiagnostic diagnostic = MeasureLocalCandidates(input_chunk);
    if (diagnostic.best_base_fp.empty() || diagnostic.best_delta.empty()) {
        input_chunk->info.stat = NON_SIMILAR_CHUNK;
        RecordMissedCandidate(diagnostic);
        return false;
    }

    const string& selected = diagnostic.best_base_fp;
    // Refresh only feature/base associations that actually produced the best
    // candidate. This preserves exact per-client cache ownership.
    for (size_t i = 0; i < SUPER_FEATURE_PER_CHUNK; i++) {
        uint64_t feature = input_chunk->info.features[i];
        bool matched = false;
        auto feature_it = unified_feature_2_fp_db_.find(feature);
        if (feature_it != unified_feature_2_fp_db_.end()) {
            matched = find(feature_it->second.begin(), feature_it->second.end(),
                selected) != feature_it->second.end();
        }
        if (!matched) {
            matched = similar_policy_->FeatureContainsBase(
                global_feature_2_fp_db_, feature, selected);
        }
        if (matched) AddAssociation(client_id, feature, selected, false);
    }

    output_chunk->info.size = diagnostic.best_delta_size;
    output_chunk->info.logical_size = input_chunk->info.size;
    memcpy(output_chunk->data, diagnostic.best_delta.data(),
        diagnostic.best_delta_size);
    memcpy(output_chunk->info.addr.base_fp, selected.data(), CHUNK_HASH_SIZE);
    memcpy(output_chunk->info.fp, input_chunk->info.fp, CHUNK_HASH_SIZE);
    memcpy(output_chunk->info.compressed_fp, input_chunk->info.compressed_fp,
        CHUNK_HASH_SIZE);
    output_chunk->info.stat = CACHE_DELTA_CHUNK;
    input_chunk->info.stat = SIMILAR_CHUNK;
    RecordSelectedCandidate(diagnostic, selected, output_chunk->info.size,
        input_chunk->info.size);
    return true;
}

/**
 * @brief insert a cached chunk (dual-write: memory + LevelDB)
 *
 * @param cache_chunk cache chunk to insert
 */
void InformCache::InsertCachedChunk(WrappedChunk_t* cache_chunk,
    uint32_t client_id, bool update_global) {
    boost::mutex::scoped_lock lock(cache_lock_);

    string base_fp_str;
    base_fp_str.assign((char*)cache_chunk->info.fp, CHUNK_HASH_SIZE);

    // Store the base once. Reference counts are increased only when a new,
    // exact client/feature/base association is added below.
    if (unified_base_2_cnt_idx_.find(base_fp_str) ==
        unified_base_2_cnt_idx_.end()) {
        unified_base_2_cnt_idx_[base_fp_str] = {0, cache_chunk->info.size};
        cache_base_chunk_str_.assign((char*)cache_chunk->data,
            cache_chunk->info.size);
        unified_base_2_data_db_->Insert(base_fp_str, cache_base_chunk_str_);
    }

    for (size_t i = 0; i < SUPER_FEATURE_PER_CHUNK; i++) {
        AddAssociation(client_id, cache_chunk->info.features[i], base_fp_str,
            update_global);
    }

    return ;
}

/**
 * @brief evict features from the cache (decrement ref counts)
 *
 * @param evict_chunk eviction data (features to remove)
 */
void InformCache::EvictCacheChunk(WrappedChunk_t* evict_chunk,
    uint32_t client_id) {
    boost::mutex::scoped_lock lock(cache_lock_);

    uint32_t feature_num = evict_chunk->info.size;
    uint64_t* feature_ptr;

    for (size_t i = 0; i < feature_num; i++) {
        feature_ptr = (uint64_t*)(evict_chunk->data + i * sizeof(uint64_t));
        // An eviction list belongs to one client. Removing the entire global
        // feature bucket would incorrectly discard bases still used by other
        // clients, so remove only this client's exact associations.
        RemoveClientFeature(client_id, *feature_ptr);
    }

    return ;
}

/**
 * @brief delete base chunks with zero reference count
 *
 * @return uint64_t total remaining cache size
 */
uint64_t InformCache::DeleteEvictChunk() {
    boost::mutex::scoped_lock lock(cache_lock_);

    uint64_t total_cache_size = 0;
    auto it = unified_base_2_cnt_idx_.begin();
    while (it != unified_base_2_cnt_idx_.end()) {
        if (it->second.first == 0) {
            unified_base_2_data_db_->Delete(it->first);
            it = unified_base_2_cnt_idx_.erase(it);
        } else {
            total_cache_size += it->second.second;
            it++;
        }
    }

    return total_cache_size;
}

/**
 * @brief get the number of features in the unified cache
 *
 * @return uint64_t cache feature count
 */
uint64_t InformCache::GetCacheSize() {
    boost::mutex::scoped_lock lock(cache_lock_);
    return unified_feature_2_fp_db_.size();
}

// ========== 下载路径 (静态方法) ==========

/**
 * @brief check if a base chunk exists in the unified cache
 *
 * @param base_fp base chunk fingerprint
 * @return true exists
 * @return false not exists
 */
bool InformCache::IsBaseChunkExist(uint8_t* base_fp) {
    boost::mutex::scoped_lock lock(cache_lock_);

    string base_fp_str;
    base_fp_str.assign((char*)base_fp, CHUNK_HASH_SIZE);
    auto it = unified_base_2_cnt_idx_.find(base_fp_str);
    return it != unified_base_2_cnt_idx_.end() && it->second.first > 0;
}

/**
 * @brief fetch a base chunk from the unified cache
 *
 * @param base_fp base chunk fingerprint
 * @param output_base output buffer for base chunk data
 * @return uint32_t base chunk size
 */
uint32_t InformCache::FetchBaseChunk(uint8_t* base_fp, uint8_t* output_base) {
    boost::mutex::scoped_lock lock(cache_lock_);

    string base_fp_str;
    base_fp_str.assign((char*)base_fp, CHUNK_HASH_SIZE);
    if (!unified_base_2_data_db_->Query(base_fp_str, cache_base_chunk_str_)) {
        tool::Logging("InformCache",
            "cannot find the base chunk in the unified cache.\n");
        exit(EXIT_FAILURE);
    }
    uint32_t base_chunk_size = cache_base_chunk_str_.size();
    memcpy(output_base, cache_base_chunk_str_.c_str(), base_chunk_size);
    return base_chunk_size;
}

// ========== 持久化 (静态私有方法) ==========

/**
 * @brief load the reference count index from disk
 */
void InformCache::LoadCntIdx() {
    string cnt_idx_path = cache_root_path_ + "unified_cnt";

    if (!tool::FileExist(cnt_idx_path)) {
        return;
    }

    ifstream cache_cnt_hdl;
    cache_cnt_hdl.open(cnt_idx_path, ios_base::in | ios_base::binary);
    if (!cache_cnt_hdl.is_open()) {
        tool::Logging("InformCache", "cannot open the unified cache cnt.\n");
        exit(EXIT_FAILURE);
    }

    size_t start_size = cache_cnt_hdl.tellg();
    cache_cnt_hdl.seekg(0, ios_base::end);
    size_t file_size = cache_cnt_hdl.tellg();
    file_size = file_size - start_size;

    if (file_size == 0) {
        cache_cnt_hdl.close();
        return;
    }
    cache_cnt_hdl.seekg(0, ios_base::beg);

    size_t idx_item_num = 0;
    cache_cnt_hdl.read((char*)&idx_item_num, sizeof(size_t));
    if (idx_item_num == 0) {
        cache_cnt_hdl.close();
        return;
    }

    string base_fp_str;
    base_fp_str.resize(CHUNK_HASH_SIZE, 0);
    uint32_t cnt;
    uint32_t chunk_size;
    for (size_t i = 0; i < idx_item_num; i++) {
        cache_cnt_hdl.read((char*)&base_fp_str[0], CHUNK_HASH_SIZE);
        cache_cnt_hdl.read((char*)&cnt, sizeof(uint32_t));
        cache_cnt_hdl.read((char*)&chunk_size, sizeof(uint32_t));
        unified_base_2_cnt_idx_[base_fp_str] = {cnt, chunk_size};
    }

    cache_cnt_hdl.close();
    tool::Logging("InformCache", "loaded %lu ref-count entries from %s\n",
        idx_item_num, cnt_idx_path.c_str());
}

/**
 * @brief store the reference count index to disk
 */
void InformCache::StoreCntIdx() {
    string cnt_idx_path = cache_root_path_ + "unified_cnt";
    ofstream cache_cnt_hdl;
    cache_cnt_hdl.open(cnt_idx_path, ios_base::trunc | ios_base::binary);
    if (!cache_cnt_hdl.is_open()) {
        tool::Logging("InformCache", "cannot init the unified cache cnt idx.\n");
        exit(EXIT_FAILURE);
    }

    size_t idx_item_num = unified_base_2_cnt_idx_.size();
    cache_cnt_hdl.write((char*)&idx_item_num, sizeof(size_t));

    for (auto it : unified_base_2_cnt_idx_) {
        cache_cnt_hdl.write(it.first.c_str(), CHUNK_HASH_SIZE);
        cache_cnt_hdl.write((char*)&it.second.first, sizeof(uint32_t));
        cache_cnt_hdl.write((char*)&it.second.second, sizeof(uint32_t));
    }
    cache_cnt_hdl.close();

    tool::Logging("InformCache", "stored %lu ref-count entries to %s\n",
        idx_item_num, cnt_idx_path.c_str());
}

bool InformCache::LoadAssociations() {
    string assoc_path = cache_root_path_ + "unified_assoc";
    if (!tool::FileExist(assoc_path)) return false;

    ifstream assoc_hdl(assoc_path, ios_base::in | ios_base::binary);
    if (!assoc_hdl.is_open()) return false;

    const char expected_magic[8] = {'E', 'D', 'R', 'A', 'S', 'S', 'O', 'C'};
    char magic[8];
    uint32_t version = 0;
    uint64_t association_num = 0;
    assoc_hdl.read(magic, sizeof(magic));
    assoc_hdl.read((char*)&version, sizeof(version));
    assoc_hdl.read((char*)&association_num, sizeof(association_num));
    if (!assoc_hdl.good() || memcmp(magic, expected_magic, sizeof(magic)) != 0 ||
        version != 1) {
        tool::Logging("InformCache", "invalid association metadata: %s\n",
            assoc_path.c_str());
        return false;
    }

    unified_feature_2_fp_db_.clear();
    client_feature_2_fp_db_.clear();
    feature_base_ref_cnt_.clear();
    persisted_feature_base_ref_cnt_.clear();
    for (auto& item : unified_base_2_cnt_idx_) item.second.first = 0;

    for (uint64_t i = 0; i < association_num; i++) {
        uint32_t client_id;
        uint64_t feature;
        string base_fp(CHUNK_HASH_SIZE, 0);
        uint8_t persisted;
        assoc_hdl.read((char*)&client_id, sizeof(client_id));
        assoc_hdl.read((char*)&feature, sizeof(feature));
        assoc_hdl.read(&base_fp[0], CHUNK_HASH_SIZE);
        assoc_hdl.read((char*)&persisted, sizeof(persisted));
        if (!assoc_hdl.good()) {
            tool::Logging("InformCache", "truncated association metadata: %s\n",
                assoc_path.c_str());
            unified_feature_2_fp_db_.clear();
            client_feature_2_fp_db_.clear();
            feature_base_ref_cnt_.clear();
            persisted_feature_base_ref_cnt_.clear();
            for (auto& item : unified_base_2_cnt_idx_) item.second.first = 0;
            return false;
        }

        auto meta_it = unified_base_2_cnt_idx_.find(base_fp);
        if (meta_it == unified_base_2_cnt_idx_.end()) continue;

        auto& owner_bases = client_feature_2_fp_db_[client_id][feature];
        if (owner_bases.find(base_fp) != owner_bases.end()) continue;

        auto& candidates = unified_feature_2_fp_db_[feature];
        if (find(candidates.begin(), candidates.end(), base_fp) ==
            candidates.end()) {
            if (candidates.size() >= MAX_BASE_PER_FEATURE) continue;
            candidates.push_back(base_fp);
        }

        bool is_persisted = persisted != 0;
        owner_bases[base_fp] = is_persisted;
        feature_base_ref_cnt_[feature][base_fp]++;
        if (is_persisted) {
            persisted_feature_base_ref_cnt_[feature][base_fp]++;
        }
        meta_it->second.first++;
    }

    tool::Logging("InformCache", "loaded %lu exact cache associations from %s\n",
        association_num, assoc_path.c_str());
    return true;
}

void InformCache::StoreAssociations() {
    string assoc_path = cache_root_path_ + "unified_assoc";
    string tmp_path = assoc_path + ".tmp";
    ofstream assoc_hdl(tmp_path, ios_base::trunc | ios_base::binary);
    if (!assoc_hdl.is_open()) {
        tool::Logging("InformCache", "cannot store association metadata.\n");
        return;
    }

    uint64_t association_num = 0;
    for (const auto& client_entry : client_feature_2_fp_db_) {
        for (const auto& feature_entry : client_entry.second) {
            association_num += feature_entry.second.size();
        }
    }

    const char magic[8] = {'E', 'D', 'R', 'A', 'S', 'S', 'O', 'C'};
    const uint32_t version = 1;
    assoc_hdl.write(magic, sizeof(magic));
    assoc_hdl.write((char*)&version, sizeof(version));
    assoc_hdl.write((char*)&association_num, sizeof(association_num));
    for (const auto& client_entry : client_feature_2_fp_db_) {
        for (const auto& feature_entry : client_entry.second) {
            for (const auto& base_entry : feature_entry.second) {
                uint8_t persisted = base_entry.second ? 1 : 0;
                assoc_hdl.write((char*)&client_entry.first,
                    sizeof(client_entry.first));
                assoc_hdl.write((char*)&feature_entry.first,
                    sizeof(feature_entry.first));
                assoc_hdl.write(base_entry.first.data(), CHUNK_HASH_SIZE);
                assoc_hdl.write((char*)&persisted, sizeof(persisted));
            }
        }
    }
    assoc_hdl.flush();
    assoc_hdl.close();

    if (rename(tmp_path.c_str(), assoc_path.c_str()) != 0) {
        tool::Logging("InformCache", "cannot publish association metadata.\n");
        return;
    }
    tool::Logging("InformCache", "stored %lu exact cache associations to %s\n",
        association_num, assoc_path.c_str());
}
