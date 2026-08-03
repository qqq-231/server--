/**
 * @file inform_cache.h
 * @author Zuoru YANG (zryang@cse.cuhk.edu.hk)
 * @brief define the interfaces (global unified shared architecture)
 * @version 0.2
 * @date 2022-08-02
 *
 * @copyright Copyright (c) 2022
 *
 */

#ifndef EDRSTORE_INFORM_CACHE_H
#define EDRSTORE_INFORM_CACHE_H

#include <boost/thread/thread.hpp>

#include "../configure.h"
#include "../data_structure.h"
#include "../database/db_factory.h"
#include "../chunker/finesse_util.h"
#include "../reduction/similar_policy.h"
#include "../reduction/delta_comp.h"

extern Configure config;

class InformCache {
    private:
        string my_name_ = "InformCache";

        // ========== 全局共享静态成员 (所有客户端共用) ==========
        static unordered_map<uint64_t, vector<string>> unified_feature_2_fp_db_;
        static unordered_map<string, pair<uint32_t, uint32_t>> unified_base_2_cnt_idx_;
        // client -> feature -> base_fp -> whether the association is persisted
        static unordered_map<uint32_t, unordered_map<uint64_t,
            unordered_map<string, bool>>> client_feature_2_fp_db_;
        // Exact reference counts for each feature/base association.
        static unordered_map<uint64_t, unordered_map<string, uint32_t>>
            feature_base_ref_cnt_;
        static unordered_map<uint64_t, unordered_map<string, uint32_t>>
            persisted_feature_base_ref_cnt_;
        static AbsDatabase* unified_base_2_data_db_;
        static AbsDatabase* global_feature_2_fp_db_;
        static boost::mutex cache_lock_;
        static string cache_root_path_;
        static string cache_base_chunk_str_;

        // Local multi-candidate selection diagnostics.
        static uint64_t local_candidate_lookup_chunk_num_;
        static uint64_t local_zero_candidate_chunk_num_;
        static uint64_t local_single_candidate_chunk_num_;
        static uint64_t local_multi_candidate_chunk_num_;
        static uint64_t local_total_candidate_num_;
        static uint64_t local_max_candidate_num_;
        static uint64_t local_live_candidate_num_;
        static uint64_t local_stale_candidate_num_;
        static uint64_t local_missing_data_candidate_num_;
        static uint64_t local_delta_evaluation_num_;
        static uint64_t local_selected_not_best_num_;
        static uint64_t local_potential_saved_bytes_;
        static uint64_t local_missed_live_best_num_;
        static uint64_t local_selected_below_threshold_num_;
        static uint64_t local_best_avoids_reject_num_;
        static uint64_t local_bucket_eviction_num_;

        struct CandidateDiagnostic {
            string best_base_fp;
            uint32_t best_delta_size = 0;
            vector<uint8_t> best_delta;
        };

        static void LoadCntIdx();
        static void StoreCntIdx();
        static bool LoadAssociations();
        static void StoreAssociations();

        void AddAssociation(uint32_t client_id, uint64_t feature,
            const string& base_fp, bool persist);
        void RemoveClientFeature(uint32_t client_id, uint64_t feature);
        void RemoveAssociationForAllClients(uint64_t feature,
            const string& base_fp);
        void RemoveBaseIfUnused(const string& base_fp);
        bool IsBaseLive(const string& base_fp);
        CandidateDiagnostic MeasureLocalCandidates(
            WrappedChunk_t* input_chunk);
        void RecordSelectedCandidate(const CandidateDiagnostic& diagnostic,
            const string& selected_base_fp, uint32_t selected_delta_size,
            uint32_t input_size);
        void RecordMissedCandidate(const CandidateDiagnostic& diagnostic);
        static void ReportLocalCandidateStats();

        // ========== 无状态工具成员 (实例持有，线程安全) ==========
        FinesseUtil* finesse_util_;
        RabinFPUtil* rabin_util_;
        RabinCtx_t rabin_ctx_;
        SimilarPolicy* similar_policy_;
        DeltaComp* delta_comp_;

    public:
        // 全局生命周期管理
        static void InitGlobalCache(AbsDatabase* feature_2_fp_db);
        static void DestroyGlobalCache();

        /**
         * @brief Construct a new InformCache object (轻量级，仅创建无状态工具)
         */
        InformCache();

        /**
         * @brief Destroy the InformCache object
         */
        ~InformCache();

        // ========== 上传路径 (需要实例：使用 similar_policy_ / delta_comp_) ==========

        bool ProcessNormalChunk(WrappedChunk_t* input_chunk,
            WrappedChunk_t* output_chunk, uint32_t client_id);
        void InsertCachedChunk(WrappedChunk_t* cache_chunk,
            uint32_t client_id, bool update_global = true);
        void EvictCacheChunk(WrappedChunk_t* evict_chunk,
            uint32_t client_id);
        uint64_t DeleteEvictChunk();
        uint64_t GetCacheSize();

        // ========== 下载路径 (静态方法：无需实例) ==========

        static uint32_t FetchBaseChunk(uint8_t* base_fp, uint8_t* output_base);
        static bool IsBaseChunkExist(uint8_t* base_fp);
};

#endif
