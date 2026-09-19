/**
 * @file similar_policy.cc
 * @author Zuoru YANG (zryang@cse.cuhk.edu.hk)
 * @brief implement the interfaces of SimilarPolicy
 * @version 0.1
 * @date 2022-08-03
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include "../../include/reduction/similar_policy.h"
#include "../../include/reduction/binary_tree_index.h"
#include"../../include/const_var.h"
bool CmpPair(pair<string, uint32_t>& a, 
    pair<string, uint32_t>& b) {
    return a.second > b.second;
}

/**
 * @brief Construct a new SimilarPolicy object
 * 
 */
SimilarPolicy::SimilarPolicy() {

}

/**
 * @brief Destroy the SimilarPolicy object
 * 
 */
SimilarPolicy::~SimilarPolicy() {

}

void SimilarPolicy::RecordSimilarityDetection(
    const chrono::steady_clock::time_point& start_time) {
    const auto elapsed_us = chrono::duration_cast<chrono::microseconds>(
        chrono::steady_clock::now() - start_time).count();
    similarity_detection_time_us_.fetch_add(
        static_cast<uint64_t>(elapsed_us), memory_order_relaxed);
}

uint64_t SimilarPolicy::GetSimilarityCompareCount() const {
    return similarity_compare_count_.load(memory_order_relaxed);
}

double SimilarPolicy::GetSimilarityDetectionTimeMs() const {
    return static_cast<double>(similarity_detection_time_us_.load(
        memory_order_relaxed)) / 1000.0;
}

double SimilarPolicy::GetCandidateSearchTimeMs() const {
    return static_cast<double>(candidate_search_time_ns_.load(
        memory_order_relaxed)) / 1000000.0;
}

double SimilarPolicy::GetCandidateFilteringTimeMs() const {
    return static_cast<double>(candidate_filtering_time_ns_.load(
        memory_order_relaxed)) / 1000000.0;
}

double SimilarPolicy::GetSimilarityCompareTimeMs() const {
    return static_cast<double>(similarity_compare_time_ns_.load(
        memory_order_relaxed)) / 1000000.0;
}

bool SimilarPolicy::DecodeFeatureCandidates(const string& stored_ids,
    FeatureCandidateList& candidates) const {
    candidates.count = 0;
    if (stored_ids.empty() || stored_ids.size() % CHUNK_HASH_SIZE != 0) {
        return false;
    }

    const size_t stored_count = stored_ids.size() / CHUNK_HASH_SIZE;
    // Old databases may have been produced with a larger N. Keep the newest
    // configured candidates because the serialized order is FIFO.
    const size_t first = stored_count > CIPHER_FEATURE_MAX_ID_NUM ?
        stored_count - CIPHER_FEATURE_MAX_ID_NUM : 0;
    candidates.count = stored_count - first;
    for (size_t i = 0; i < candidates.count; i++) {
        memcpy(candidates.ids[i].data(),
            stored_ids.data() + (first + i) * CHUNK_HASH_SIZE,
            CHUNK_HASH_SIZE);
    }
    return true;
}

void SimilarPolicy::UpdateFeatureIdCountStat(bool old_entry_exists,
    size_t old_count, size_t new_count) {
    if (old_entry_exists) {
        auto old_it = feature_id_count_stat_.find(old_count);
        if (old_it != feature_id_count_stat_.end() && old_it->second > 0) {
            old_it->second--;
        }
    }
    feature_id_count_stat_[new_count]++;
}

void SimilarPolicy::InitializeFeatureIdCountStat(
    const map<size_t, uint64_t>& feature_id_count_stat) {
    lock_guard<mutex> update_lock(feature_index_update_mutex_);
    feature_id_count_stat_ = feature_id_count_stat;
}

map<size_t, uint64_t> SimilarPolicy::GetFeatureIdCountStat() const {
    lock_guard<mutex> update_lock(feature_index_update_mutex_);
    return feature_id_count_stat_;
}

/**
 * @brief find the base chunk
 * 
 * @param feature_2_fp_db feature to base fp index
 * @param info chunk info
 */
void SimilarPolicy::FindBaseChunk(AbsDatabase* feature_2_fp_db,
    ChunkInfo_t* info) {
    const auto detection_start = chrono::steady_clock::now();
    uint64_t candidate_search_ns = 0;
    uint64_t candidate_filtering_ns = 0;
    constexpr size_t kMaxCandidateNum = SUPER_FEATURE_PER_CHUNK *
        CIPHER_FEATURE_MAX_ID_NUM;
    array<CandidateVote, kMaxCandidateNum> candidate_votes{};
    size_t candidate_num = 0;
    string stored_ids;

    for (size_t i = 0; i < SUPER_FEATURE_PER_CHUNK; i++) {
        const auto query_start = chrono::steady_clock::now();
        const bool feature_found = feature_2_fp_db->QueryBuffer(
            (char*)&info->features[i], sizeof(uint64_t), stored_ids);
        candidate_search_ns += chrono::duration_cast<chrono::nanoseconds>(
            chrono::steady_clock::now() - query_start).count();

        const auto generation_start = chrono::steady_clock::now();
        if (feature_found) {
            FeatureCandidateList feature_candidates;
            if (!this->DecodeFeatureCandidates(stored_ids,
                feature_candidates)) {
                tool::Logging(my_name_.c_str(),
                    "invalid ciphertext feature index value size: %lu.\n",
                    stored_ids.size());
                candidate_filtering_ns +=
                    chrono::duration_cast<chrono::nanoseconds>(
                        chrono::steady_clock::now() - generation_start).count();
                continue;
            }

            for (size_t id_idx = 0; id_idx < feature_candidates.count;
                id_idx++) {
                size_t vote_idx = 0;
                while (vote_idx < candidate_num &&
                    memcmp(candidate_votes[vote_idx].fp.data(),
                        feature_candidates.ids[id_idx].data(),
                        CHUNK_HASH_SIZE) != 0) {
                    vote_idx++;
                }
                if (vote_idx == candidate_num) {
                    memcpy(candidate_votes[candidate_num].fp.data(),
                        feature_candidates.ids[id_idx].data(),
                        CHUNK_HASH_SIZE);
                    candidate_votes[candidate_num].vote = 1;
                    candidate_num++;
                } else {
                    candidate_votes[vote_idx].vote++;
                }
            }
        }
        candidate_filtering_ns += chrono::duration_cast<chrono::nanoseconds>(
            chrono::steady_clock::now() - generation_start).count();
    }

    const auto compare_start = chrono::steady_clock::now();
    size_t best_candidate_idx = 0;
    uint32_t best_vote = 0;
    for (size_t i = 0; i < candidate_num; i++) {
        similarity_compare_count_.fetch_add(1, memory_order_relaxed);
        const uint32_t vote = candidate_votes[i].vote;
        if (vote > best_vote) {
            best_candidate_idx = i;
            best_vote = vote;
        }
    }
    if (best_vote >= CIPHER_FEATURE_MATCH_THRESHOLD) {
        memcpy(info->addr.base_fp,
            candidate_votes[best_candidate_idx].fp.data(), CHUNK_HASH_SIZE);
        info->stat = SIMILAR_CHUNK;
    } else {
        memset(info->addr.base_fp, 0, CHUNK_HASH_SIZE);
        info->stat = NON_SIMILAR_CHUNK;
    }
    const uint64_t similarity_compare_ns =
        chrono::duration_cast<chrono::nanoseconds>(
            chrono::steady_clock::now() - compare_start).count();
    candidate_search_time_ns_.fetch_add(candidate_search_ns,
        memory_order_relaxed);
    candidate_filtering_time_ns_.fetch_add(candidate_filtering_ns,
        memory_order_relaxed);
    similarity_compare_time_ns_.fetch_add(similarity_compare_ns,
        memory_order_relaxed);
    this->RecordSimilarityDetection(detection_start);

    return ;
}

bool SimilarPolicy::FindBaseChunk(BinaryTreeIndex* binary_tree_index,
    AbsDatabase* fp_2_addr_db, ChunkInfo_t* info) {
    const auto detection_start = chrono::steady_clock::now();
    vector<BinaryTreeCandidate> candidates;
    binary_tree_index->FindBaseCandidates(info->features, info->fp,
        candidates);
    if (candidates.empty()) {
        info->stat = NON_SIMILAR_CHUNK;
        memset(info->addr.base_fp, 0, CHUNK_HASH_SIZE);
        this->RecordSimilarityDetection(detection_start);
        return false;
    }

    const auto compare_start = chrono::steady_clock::now();
    uint32_t best_vote = 0;
    const BinaryTreeCandidate* best_candidate = nullptr;
    for (const auto& candidate : candidates) {
        similarity_compare_count_.fetch_add(1, memory_order_relaxed);

        // The address DB remains authoritative. Tree entries can outlive an
        // interrupted upload or an older on-disk index version.
        string addr_value;
        if (!fp_2_addr_db->QueryBuffer((char*)candidate.chunk_id,
            CHUNK_HASH_SIZE, addr_value) ||
            addr_value.size() != sizeof(KeyForChunkHashDB_t)) {
            continue;
        }
        const KeyForChunkHashDB_t* addr =
            reinterpret_cast<const KeyForChunkHashDB_t*>(addr_value.data());
        if (addr->stat != COMP_BASE_CHUNK &&
            addr->stat != UNCOMP_BASE_CHUNK) {
            continue;
        }

        uint32_t vote = 0;
        for (size_t query_idx = 0; query_idx < SUPER_FEATURE_PER_CHUNK;
            query_idx++) {
            for (size_t candidate_idx = 0;
                candidate_idx < SUPER_FEATURE_PER_CHUNK; candidate_idx++) {
                if (info->features[query_idx] ==
                    candidate.features[candidate_idx]) {
                    vote++;
                    break;
                }
            }
        }
        if (best_candidate == nullptr || vote > best_vote) {
            best_vote = vote;
            best_candidate = &candidate;
        }
    }

    similarity_compare_time_ns_.fetch_add(
        chrono::duration_cast<chrono::nanoseconds>(
            chrono::steady_clock::now() - compare_start).count(),
        memory_order_relaxed);
    if (best_candidate != nullptr &&
        best_vote >= CIPHER_FEATURE_MATCH_THRESHOLD) {
        memcpy(info->addr.base_fp, best_candidate->chunk_id,
            CHUNK_HASH_SIZE);
        info->stat = SIMILAR_CHUNK;
    } else {
        memset(info->addr.base_fp, 0, CHUNK_HASH_SIZE);
        info->stat = NON_SIMILAR_CHUNK;
    }
    this->RecordSimilarityDetection(detection_start);
    return best_candidate != nullptr;
}

/**
 * @brief find the base chunk
 * 
 * @param feature_2_fp_db feature to base fp index
 * @param info chunk info
 */
void SimilarPolicy::FindBaseChunk(
    unordered_map<uint64_t, string>& feature_2_fp_db,
    ChunkInfo_t* info) {
    const auto detection_start = chrono::steady_clock::now();
    uint64_t candidate_search_ns = 0;
    uint64_t candidate_filtering_ns = 0;
    // query the feature index to get the base chunk hash
    unordered_map<string, uint32_t> feature_freq_map;
    bool is_similar = false;
    bool is_first_match = true;
    uint8_t first_match_base_fp[CHUNK_HASH_SIZE];
    for (size_t i = 0; i < SUPER_FEATURE_PER_CHUNK; i++) {
        // count the freq of each base key
        const auto query_start = chrono::steady_clock::now();
        auto find_base_ret = feature_2_fp_db.find(info->features[i]);
        candidate_search_ns += chrono::duration_cast<chrono::nanoseconds>(
            chrono::steady_clock::now() - query_start).count();
        const auto generation_start = chrono::steady_clock::now();
        if (find_base_ret != feature_2_fp_db.end()) {
            if (is_first_match) {
                memcpy(first_match_base_fp, find_base_ret->second.c_str(),
                    CHUNK_HASH_SIZE);
                is_first_match = false;
            }
            auto find_freq_ret = feature_freq_map.find(find_base_ret->second);
            if (find_freq_ret != feature_freq_map.end()) {
                find_freq_ret->second++;
            } else {
                feature_freq_map[find_base_ret->second] = 1;
            }
            is_similar = true;
        }
        candidate_filtering_ns += chrono::duration_cast<chrono::nanoseconds>(
            chrono::steady_clock::now() - generation_start).count();
    }

    const auto compare_start = chrono::steady_clock::now();
    if (is_similar) {
        // similar chunk
        // find the most similar base chunk
        vector<pair<string, uint32_t>> tmp_freq_vec;
        for (auto it : feature_freq_map) {
            similarity_compare_count_.fetch_add(1, memory_order_relaxed);
            tmp_freq_vec.push_back(it);
        }
        sort(tmp_freq_vec.begin(), tmp_freq_vec.end(), CmpPair);
        if (tmp_freq_vec[0].second == 1) {
            memcpy(info->addr.base_fp, first_match_base_fp, CHUNK_HASH_SIZE);
        } else {
            memcpy(info->addr.base_fp, tmp_freq_vec[0].first.c_str(),
                CHUNK_HASH_SIZE);
        }
        info->stat = SIMILAR_CHUNK;
    } else {
        info->stat = NON_SIMILAR_CHUNK;
    }
    const uint64_t similarity_compare_ns =
        chrono::duration_cast<chrono::nanoseconds>(
            chrono::steady_clock::now() - compare_start).count();
    candidate_search_time_ns_.fetch_add(candidate_search_ns,
        memory_order_relaxed);
    candidate_filtering_time_ns_.fetch_add(candidate_filtering_ns,
        memory_order_relaxed);
    similarity_compare_time_ns_.fetch_add(similarity_compare_ns,
        memory_order_relaxed);
    this->RecordSimilarityDetection(detection_start);

    return ;
}

/**
 * @brief update the feature index 
 * 
 * @param feature_2_fp_db feature to base fp index
 * @param features chunk feature
 * @param base_fp base chunk fp
 */
//将一个新的非相似数据块的所有特征与它的基础指纹（密钥）建立映射关系，存储到数据库中。
void SimilarPolicy::UpdateFeatureIndex(AbsDatabase* feature_2_fp_db,
    uint64_t* features, uint8_t* base_fp) {
    lock_guard<mutex> update_lock(feature_index_update_mutex_);

    for (size_t i = 0; i < SUPER_FEATURE_PER_CHUNK; i++) {
        string stored_ids;
        const bool old_entry_exists = feature_2_fp_db->QueryBuffer(
            (char*)&features[i], sizeof(uint64_t), stored_ids);
        const size_t old_count = old_entry_exists &&
            stored_ids.size() % CHUNK_HASH_SIZE == 0 ?
            stored_ids.size() / CHUNK_HASH_SIZE : 0;

        FeatureCandidateList candidates;
        if (old_entry_exists &&
            !this->DecodeFeatureCandidates(stored_ids, candidates)) {
            tool::Logging(my_name_.c_str(),
                "reset invalid ciphertext feature index value size: %lu.\n",
                stored_ids.size());
        }

        bool already_exists = false;
        for (size_t id_idx = 0; id_idx < candidates.count; id_idx++) {
            if (memcmp(candidates.ids[id_idx].data(), base_fp,
                CHUNK_HASH_SIZE) == 0) {
                already_exists = true;
                break;
            }
        }
        if (already_exists) {
            continue;
        }

        if (candidates.count == CIPHER_FEATURE_MAX_ID_NUM) {
            for (size_t id_idx = 1; id_idx < candidates.count; id_idx++) {
                candidates.ids[id_idx - 1] = candidates.ids[id_idx];
            }
            candidates.count--;
        }
        memcpy(candidates.ids[candidates.count].data(), base_fp,
            CHUNK_HASH_SIZE);
        candidates.count++;

        array<char, CIPHER_FEATURE_MAX_ID_NUM * CHUNK_HASH_SIZE>
            serialized_ids{};
        for (size_t id_idx = 0; id_idx < candidates.count; id_idx++) {
            memcpy(serialized_ids.data() + id_idx * CHUNK_HASH_SIZE,
                candidates.ids[id_idx].data(), CHUNK_HASH_SIZE);
        }
        if (feature_2_fp_db->InsertBothBuffer((char*)&features[i],
            sizeof(uint64_t), serialized_ids.data(),
            candidates.count * CHUNK_HASH_SIZE)) {
            this->UpdateFeatureIdCountStat(old_entry_exists, old_count,
                candidates.count);
        } else {
            tool::Logging(my_name_.c_str(),
                "update ciphertext feature index failed.\n");
        }
    }
    return ;
}

/**
 * @brief update the feature index
 * 
 * @param feature_2_fp_db feature to base fp index
 * @param features chunk feature
 * @param base_fp base chunk fp
 */
void SimilarPolicy::UpdateFeatureIndex(
    unordered_map<uint64_t, string>& feature_2_fp_db,
    uint64_t* features, string& base_fp) {
    // non-similar chunk, update the feature index
    for (size_t i = 0; i < SUPER_FEATURE_PER_CHUNK; i++) {
        feature_2_fp_db[features[i]] = base_fp;
    }
    return ;
}
