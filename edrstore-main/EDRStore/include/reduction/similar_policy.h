/**
 * @file similar_policy.h
 * @author Zuoru YANG (zryang@cse.cuhk.edu.hk)
 * @brief define the interfaces of SimilarPolicy
 * @version 0.1
 * @date 2022-08-03
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#ifndef EDRSTORE_SIMILAR_POLICY
#define EDRSTORE_SIMILAR_POLICY

#include "../database/db_factory.h"
#include "../data_structure.h"

class BinaryTreeIndex;

class SimilarPolicy {
    private:
        using CandidateFingerprint = array<uint8_t, CHUNK_HASH_SIZE>;

        struct FeatureCandidateList {
            size_t count = 0;
            array<CandidateFingerprint, CIPHER_FEATURE_MAX_ID_NUM> ids{};
        };

        struct CandidateVote {
            CandidateFingerprint fp{};
            uint32_t vote = 0;
        };

        string my_name_ = "SimilarPolicy";
        mutable mutex feature_index_update_mutex_;
        map<size_t, uint64_t> feature_id_count_stat_;
        atomic<uint64_t> similarity_compare_count_{0};
        atomic<uint64_t> similarity_detection_time_us_{0};
        atomic<uint64_t> candidate_search_time_ns_{0};
        atomic<uint64_t> candidate_filtering_time_ns_{0};
        atomic<uint64_t> similarity_compare_time_ns_{0};

        void RecordSimilarityDetection(
            const chrono::steady_clock::time_point& start_time);
        bool DecodeFeatureCandidates(const string& stored_ids,
            FeatureCandidateList& candidates) const;
        void UpdateFeatureIdCountStat(bool old_entry_exists,
            size_t old_count, size_t new_count);

    public:
        /**
         * @brief Construct a new SimilarPolicy object
         * 
         */
        SimilarPolicy();

        /**
         * @brief Destroy the SimilarPolicy object
         * 
         */
        ~SimilarPolicy();

        uint64_t GetSimilarityCompareCount() const;
        double GetSimilarityDetectionTimeMs() const;
        double GetCandidateSearchTimeMs() const;
        double GetCandidateFilteringTimeMs() const;
        double GetSimilarityCompareTimeMs() const;

        void InitializeFeatureIdCountStat(
            const map<size_t, uint64_t>& feature_id_count_stat);
        map<size_t, uint64_t> GetFeatureIdCountStat() const;

        /**
         * @brief find the base chunk
         * 
         * @param feature_2_fp_db feature to base fp index
         * @param info chunk info
         */
        void FindBaseChunk(AbsDatabase* feature_2_fp_db,
            ChunkInfo_t* info);

        bool FindBaseChunk(BinaryTreeIndex* binary_tree_index,
            AbsDatabase* fp_2_addr_db, ChunkInfo_t* info);

        /**
         * @brief find the base chunk
         * 
         * @param feature_2_fp_db feature to base fp index
         * @param info chunk info
         */
        void FindBaseChunk(unordered_map<uint64_t, string>& feature_2_fp_db,
            ChunkInfo_t* info);

        /**
         * @brief update the feature index 
         * 
         * @param feature_2_fp_db feature to base fp index
         * @param features chunk feature
         * @param base_fp base chunk fp
         */
        void UpdateFeatureIndex(AbsDatabase* feature_2_fp_db,
            uint64_t* features, uint8_t* base_fp);

        /**
         * @brief update the feature index
         * 
         * @param feature_2_fp_db feature to base fp index
         * @param features chunk feature
         * @param base_fp base chunk fp
         */
        void UpdateFeatureIndex(unordered_map<uint64_t, string>& feature_2_fp_db,
            uint64_t* features, string& base_fp);
};

#endif
