/**
 * @file data_writer_thd.h
 * @author Zuoru YANG (zryang@cse.cuhk.edu.hk)
 * @brief define the interface of 
 * @version 0.1
 * @date 2022-07-20
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#ifndef DATA_WRITER_THD_H
#define DATA_WRITER_THD_H

#include "../configure.h"
#include "../database/db_factory.h"
#include "../reduction/delta_comp.h"
#include "../reduction/similar_policy.h"
#include "../compression/compress_util.h"
#include "client_var.h"
#include "storage_core.h"

class CacheCompThd;

class DataWriterThd {
    private:
        string my_name_ = "DataWriterThd";
    
        // storage core
        StorageCore* storage_core_;

        // for delta compression
        DeltaComp* delta_comp_;
        SimilarPolicy* similar_policy_;

        // for fp to chunk addr index
        AbsDatabase* fp_2_addr_db_;
        AbsDatabase* compressed_feature_2_fp_db_;
        // All DataWriterThd instances share the same feature DB.  The update
        // is a read-modify-write operation, so the lock must be process-wide.
        static boost::mutex compressed_index_lck_;
        boost::mutex stat_lck_;
        CacheCompThd* cache_comp_thd_;

        struct CompressedDeltaResult {
            string base_fp;
            uint32_t size = 0;
            vector<uint8_t> data;
        };

        uint64_t deferred_compare_num_ = 0;
        uint64_t deferred_choose_local_num_ = 0;
        uint64_t deferred_choose_full_num_ = 0;
        uint64_t deferred_choose_comp_delta_num_ = 0;
        uint64_t deferred_saved_vs_local_ = 0;

        bool FindBestCompressedDelta(WrappedChunk_t* input_chunk,
            ClientVar* cur_client, CompressedDeltaResult& result);
        void WriteCompressedDelta(WrappedChunk_t* input_chunk,
            CompressedDeltaResult& result, ClientVar* cur_client);
        bool ProcDeferredComparison(WrappedChunk_t* input_chunk,
            ClientVar* cur_client);
        void RecordGlobalDelta(uint32_t input_size, uint32_t delta_size);

        /** Try a one-level delta in the compressed-ciphertext domain. */
        bool ProcCompressedSimilarChunk(WrappedChunk_t* input_chunk,
            ClientVar* cur_client);

        /** Remove an invalid candidate from the buckets hit by this chunk. */
        void PruneCompressedCandidate(uint64_t* features,
            const string& base_fp);

        /**
         * @brief process a non-similar chunk
         *
         * @param input_chunk input chunk
         * @param cur_client current client
         */
        void ProcNonSimilarChunk(WrappedChunk_t* input_chunk, ClientVar* cur_client);

        /**
         * @brief process a cache delta chunk
         *
         * @param input_chunk input chunk
         * @param cur_client current client
         */
        void ProcCacheDeltaChunk(WrappedChunk_t* input_chunk, ClientVar* cur_client);

    public:
        // for delta compression
        uint64_t _total_similar_chunk_num = 0;
        uint64_t _total_similar_data_size = 0;
        uint64_t _total_delta_size = 0;
        uint64_t _similarity_hist[7] = {0};

#ifdef EDR_BREAKDOWN
        struct timeval _comp_delta_stime;
        struct timeval _comp_delta_etime;
        double _total_comp_delta_time = 0;
        uint64_t _total_comp_delta_data_size = 0;
#endif

        /**
         * @brief Construct a new DataWriterThd object
         * 
         * @param fp_2_addr_db fp to chunk addr index
         * @param compressed_feature_2_fp_db compressed ciphertext feature index
         * @param storage_core storage core
         */
        DataWriterThd(AbsDatabase* fp_2_addr_db,
            AbsDatabase* compressed_feature_2_fp_db, StorageCore* storage_core,
            CacheCompThd* cache_comp_thd);

        /**
         * @brief Destroy the DataWriterThd object
         * 
         */
        ~DataWriterThd();

        /**
         * @brief the main process
         * 
         * @param cur_client current client var
         */
        void Run(ClientVar* cur_client);
};

#endif
