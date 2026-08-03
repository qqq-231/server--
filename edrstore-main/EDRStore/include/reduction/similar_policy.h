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

class SimilarPolicy {
    private:
        string my_name_ = "SimilarPolicy";

    public:
        // 埋点统计：MAX_BASE_PER_FEATURE 导致的 FIFO 淘汰计数
        static uint64_t evict_total_calls_;       // vec.size()>=MAX_BASE_PER_FEATURE 触发次数
        static uint64_t backfill_evict_calls_;    // ProcessNormalChunk 回填路径触发次数

        static void ReportEvictStats();
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

        /**
         * @brief find the base chunk
         * 
         * @param feature_2_fp_db feature to base fp index
         * @param info chunk info
         */
        void FindBaseChunk(AbsDatabase* feature_2_fp_db,
            ChunkInfo_t* info);

        /**
         * @brief collect all unique candidates ordered by feature votes
         *
         * The caller may evaluate the actual delta size instead of assuming
         * that the highest-vote candidate is also the best compression base.
         */
        void GetBaseCandidates(AbsDatabase* feature_2_fp_db,
            uint64_t* features, vector<string>& candidates);

        /** Collect candidates from an in-memory 1:N feature index. */
        void GetBaseCandidates(
            unordered_map<uint64_t, vector<string>>& feature_2_fp_db,
            uint64_t* features, vector<string>& candidates);

        /**
         * @brief find the base chunk
         * 
         * @param feature_2_fp_db feature to base fp index
         * @param info chunk info
         */
        void FindBaseChunk(unordered_map<uint64_t, vector<string>>& feature_2_fp_db,
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
        void UpdateFeatureIndex(unordered_map<uint64_t, vector<string>>& feature_2_fp_db,
            uint64_t* features, string& base_fp);

        /** Update or remove one exact feature-to-base association. */
        void UpdateFeatureIndex(AbsDatabase* feature_2_fp_db,
            uint64_t feature, const string& base_fp);
        void RemoveFeatureIndex(AbsDatabase* feature_2_fp_db,
            uint64_t feature, const string& base_fp);

        /** Check whether a persisted feature bucket contains a base. */
        bool FeatureContainsBase(AbsDatabase* feature_2_fp_db,
            uint64_t feature, const string& base_fp);
};

#endif
