/**
 * @file similar_policy.cc
 * @author Zuoru YANG (zryang@cse.cuhk.edu.hk)
 * @brief implement the interfaces of SimilarPolicy (1:N feature index)
 * @version 0.2
 * @date 2022-08-03
 *
 * @copyright Copyright (c) 2022
 *
 */

#include "../../include/reduction/similar_policy.h"
#include "../../include/const_var.h"

#include <iostream>

uint64_t SimilarPolicy::evict_total_calls_ = 0;
uint64_t SimilarPolicy::backfill_evict_calls_ = 0;

void SimilarPolicy::ReportEvictStats() {
    std::cout << "\n"
              << "========== MAX_BASE_PER_FEATURE Eviction Stats ==========\n"
              << "  UpdateFeatureIndex FIFO evictions:  " << evict_total_calls_ << "\n"
              << "  ProcessNormalChunk backfill evictions: " << backfill_evict_calls_ << "\n"
              << "  Total buckets overflowed:             " << (evict_total_calls_ + backfill_evict_calls_) << "\n"
              << "=========================================================\n"
              << std::endl;
}

bool CmpPair(pair<string, uint32_t>& a,
    pair<string, uint32_t>& b) {
    if (a.second != b.second) return a.second > b.second;
    return a.first < b.first;
}

// ---------- AbsDatabase serialization helpers ----------

static void DeserializeBaseFpList(const string& value,
    vector<string>& out_list) {
    if (value.size() < sizeof(uint32_t)) return;
    uint32_t count = *(uint32_t*)&value[0];
    out_list.clear();
    out_list.reserve(count);
    size_t offset = sizeof(uint32_t);
    for (uint32_t i = 0; i < count; i++) {
        if (offset + CHUNK_HASH_SIZE > value.size()) break;
        out_list.emplace_back(&value[offset], CHUNK_HASH_SIZE);
        offset += CHUNK_HASH_SIZE;
    }
}

static string SerializeBaseFpList(const vector<string>& list) {
    string value;
    uint32_t count = min((uint32_t)list.size(), (uint32_t)MAX_BASE_PER_FEATURE);
    value.append((char*)&count, sizeof(uint32_t));
    for (uint32_t i = 0; i < count; i++) {
        value.append(list[i]);
    }
    return value;
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

/**
 * @brief find the base chunk (AbsDatabase version)
 *
 * @param feature_2_fp_db feature to base fp index
 * @param info chunk info
 */
void SimilarPolicy::FindBaseChunk(AbsDatabase* feature_2_fp_db,
    ChunkInfo_t* info) {
    unordered_map<string, uint32_t> feature_freq_map;
    bool is_similar = false;
    string tmp_value;
    vector<string> candidate_list;

    for (size_t i = 0; i < SUPER_FEATURE_PER_CHUNK; i++) {
        if (feature_2_fp_db->QueryBuffer((char*)&info->features[i],
            sizeof(uint64_t), tmp_value)) {
            DeserializeBaseFpList(tmp_value, candidate_list);
            for (auto& base_fp : candidate_list) {
                if (feature_freq_map.find(base_fp) != feature_freq_map.end()) {
                    feature_freq_map[base_fp]++;
                } else {
                    feature_freq_map[base_fp] = 1;
                }
                is_similar = true;
            }
        }
    }

    if (is_similar) {
        vector<pair<string, uint32_t>> tmp_freq_vec;
        for (auto it : feature_freq_map) {
            tmp_freq_vec.push_back(it);
        }
        sort(tmp_freq_vec.begin(), tmp_freq_vec.end(), CmpPair);
        if (tmp_freq_vec[0].second < SIMILAR_VOTE_THRESHOLD) {
            info->stat = NON_SIMILAR_CHUNK;
        } else {
            memcpy(info->addr.base_fp, tmp_freq_vec[0].first.c_str(),
                CHUNK_HASH_SIZE);
            info->stat = SIMILAR_CHUNK;
        }
    } else {
        info->stat = NON_SIMILAR_CHUNK;
    }

    return ;
}

void SimilarPolicy::GetBaseCandidates(AbsDatabase* feature_2_fp_db,
    uint64_t* features, vector<string>& candidates) {
    unordered_map<string, uint32_t> feature_freq_map;
    string value;
    vector<string> bucket;

    candidates.clear();
    for (size_t i = 0; i < SUPER_FEATURE_PER_CHUNK; i++) {
        if (!feature_2_fp_db->QueryBuffer((char*)&features[i],
            sizeof(uint64_t), value)) {
            continue;
        }
        bucket.clear();
        DeserializeBaseFpList(value, bucket);
        for (const auto& base_fp : bucket) feature_freq_map[base_fp]++;
    }

    vector<pair<string, uint32_t>> ranked(feature_freq_map.begin(),
        feature_freq_map.end());
    sort(ranked.begin(), ranked.end(), CmpPair);
    for (const auto& item : ranked) {
        if (item.second >= SIMILAR_VOTE_THRESHOLD) {
            candidates.push_back(item.first);
        }
    }
}

void SimilarPolicy::GetBaseCandidates(
    unordered_map<uint64_t, vector<string>>& feature_2_fp_db,
    uint64_t* features, vector<string>& candidates) {
    unordered_map<string, uint32_t> feature_freq_map;

    candidates.clear();
    for (size_t i = 0; i < SUPER_FEATURE_PER_CHUNK; i++) {
        auto bucket = feature_2_fp_db.find(features[i]);
        if (bucket == feature_2_fp_db.end()) continue;
        for (const auto& base_fp : bucket->second) {
            feature_freq_map[base_fp]++;
        }
    }

    vector<pair<string, uint32_t>> ranked(feature_freq_map.begin(),
        feature_freq_map.end());
    sort(ranked.begin(), ranked.end(), CmpPair);
    for (const auto& item : ranked) {
        if (item.second >= SIMILAR_VOTE_THRESHOLD) {
            candidates.push_back(item.first);
        }
    }
}

/**
 * @brief find the base chunk (unordered_map version)
 *
 * @param feature_2_fp_db feature to base fp index (1:N)
 * @param info chunk info
 */
void SimilarPolicy::FindBaseChunk(
    unordered_map<uint64_t, vector<string>>& feature_2_fp_db,
    ChunkInfo_t* info) {
    unordered_map<string, uint32_t> feature_freq_map;
    bool is_similar = false;

    for (size_t i = 0; i < SUPER_FEATURE_PER_CHUNK; i++) {
        auto find_base_ret = feature_2_fp_db.find(info->features[i]);
        if (find_base_ret != feature_2_fp_db.end()) {
            for (auto& base_fp : find_base_ret->second) {
                auto find_freq_ret = feature_freq_map.find(base_fp);
                if (find_freq_ret != feature_freq_map.end()) {
                    find_freq_ret->second++;
                } else {
                    feature_freq_map[base_fp] = 1;
                }
                is_similar = true;
            }
        }
    }

    if (is_similar) {
        vector<pair<string, uint32_t>> tmp_freq_vec;
        for (auto it : feature_freq_map) {
            tmp_freq_vec.push_back(it);
        }
        sort(tmp_freq_vec.begin(), tmp_freq_vec.end(), CmpPair);
        if (tmp_freq_vec[0].second < SIMILAR_VOTE_THRESHOLD) {
            info->stat = NON_SIMILAR_CHUNK;
        } else {
            memcpy(info->addr.base_fp, tmp_freq_vec[0].first.c_str(),
                CHUNK_HASH_SIZE);
            info->stat = SIMILAR_CHUNK;
        }
    } else {
        info->stat = NON_SIMILAR_CHUNK;
    }

    return ;
}

/**
 * @brief update the feature index (AbsDatabase version)
 *
 * @param feature_2_fp_db feature to base fp index
 * @param features chunk feature
 * @param base_fp base chunk fp
 */
void SimilarPolicy::UpdateFeatureIndex(AbsDatabase* feature_2_fp_db,
    uint64_t* features, uint8_t* base_fp) {
    string new_base_fp_str((char*)base_fp, CHUNK_HASH_SIZE);
    string existing_value;
    vector<string> candidate_list;

    for (size_t i = 0; i < SUPER_FEATURE_PER_CHUNK; i++) {
        if (feature_2_fp_db->QueryBuffer((char*)&features[i],
            sizeof(uint64_t), existing_value)) {
            DeserializeBaseFpList(existing_value, candidate_list);
            bool already_exists = false;
            for (auto& fp : candidate_list) {
                if (fp == new_base_fp_str) { already_exists = true; break; }
            }
            if (!already_exists) {
                if (candidate_list.size() >= MAX_BASE_PER_FEATURE) {
                    candidate_list.erase(candidate_list.begin());
                    evict_total_calls_++;
                }
                candidate_list.push_back(new_base_fp_str);
            }
        } else {
            candidate_list = {new_base_fp_str};
        }
        string serialized = SerializeBaseFpList(candidate_list);
        feature_2_fp_db->InsertBothBuffer((char*)&features[i],
            sizeof(uint64_t), (char*)serialized.c_str(), serialized.size());
    }
    return ;
}

/**
 * @brief update the feature index (unordered_map version)
 *
 * @param feature_2_fp_db feature to base fp index (1:N)
 * @param features chunk feature
 * @param base_fp base chunk fp
 */
void SimilarPolicy::UpdateFeatureIndex(
    unordered_map<uint64_t, vector<string>>& feature_2_fp_db,
    uint64_t* features, string& base_fp) {
    for (size_t i = 0; i < SUPER_FEATURE_PER_CHUNK; i++) {
        auto& vec = feature_2_fp_db[features[i]];
        bool already_exists = false;
        for (auto& fp : vec) {
            if (fp == base_fp) { already_exists = true; break; }
        }
        if (!already_exists) {
            if (vec.size() >= MAX_BASE_PER_FEATURE) {
                vec.erase(vec.begin());
                evict_total_calls_++;
            }
            vec.push_back(base_fp);
        }
    }
    return ;
}

void SimilarPolicy::UpdateFeatureIndex(AbsDatabase* feature_2_fp_db,
    uint64_t feature, const string& base_fp) {
    string existing_value;
    vector<string> candidate_list;

    if (feature_2_fp_db->QueryBuffer((char*)&feature, sizeof(uint64_t),
        existing_value)) {
        DeserializeBaseFpList(existing_value, candidate_list);
    }

    auto existing = find(candidate_list.begin(), candidate_list.end(), base_fp);
    if (existing == candidate_list.end()) {
        if (candidate_list.size() >= MAX_BASE_PER_FEATURE) {
            candidate_list.erase(candidate_list.begin());
            evict_total_calls_++;
        }
        candidate_list.push_back(base_fp);
    }

    string serialized = SerializeBaseFpList(candidate_list);
    feature_2_fp_db->InsertBothBuffer((char*)&feature, sizeof(uint64_t),
        serialized.data(), serialized.size());
}

void SimilarPolicy::RemoveFeatureIndex(AbsDatabase* feature_2_fp_db,
    uint64_t feature, const string& base_fp) {
    string existing_value;
    vector<string> candidate_list;
    if (!feature_2_fp_db->QueryBuffer((char*)&feature, sizeof(uint64_t),
        existing_value)) {
        return;
    }

    DeserializeBaseFpList(existing_value, candidate_list);
    candidate_list.erase(remove(candidate_list.begin(), candidate_list.end(),
        base_fp), candidate_list.end());

    if (candidate_list.empty()) {
        feature_2_fp_db->DeleteBuffer((char*)&feature, sizeof(uint64_t));
        return;
    }

    string serialized = SerializeBaseFpList(candidate_list);
    feature_2_fp_db->InsertBothBuffer((char*)&feature, sizeof(uint64_t),
        serialized.data(), serialized.size());
}

bool SimilarPolicy::FeatureContainsBase(AbsDatabase* feature_2_fp_db,
    uint64_t feature, const string& base_fp) {
    string existing_value;
    vector<string> candidate_list;
    if (!feature_2_fp_db->QueryBuffer((char*)&feature, sizeof(uint64_t),
        existing_value)) {
        return false;
    }
    DeserializeBaseFpList(existing_value, candidate_list);
    return find(candidate_list.begin(), candidate_list.end(), base_fp) !=
        candidate_list.end();
}
