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
#include "client_var.h"
#include "storage_core.h"

class DataWriterThd {
    private:
        string my_name_ = "DataWriterThd";

        // storage core
        StorageCore* storage_core_;

        // for fp to chunk addr index
        AbsDatabase* fp_2_addr_db_;

        /**
         * @brief process a cache delta chunk
         *
         * @param input_chunk input chunk
         * @param cur_client current client
         */
        void ProcCacheDeltaChunk(WrappedChunk_t* input_chunk, ClientVar* cur_client);

        /**
         * @brief process a unique chunk (write as base)
         *
         * @param input_chunk input chunk
         * @param cur_client current client
         */
        void ProcUniqueChunk(WrappedChunk_t* input_chunk, ClientVar* cur_client);

    public:
        // stats (all zero since similarity is in stage 1)
        uint64_t _total_similar_chunk_num = 0;
        uint64_t _total_similar_data_size = 0;
        uint64_t _total_delta_size = 0;

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
         * @param storage_core storage core
         */
        DataWriterThd(AbsDatabase* fp_2_addr_db,
            StorageCore* storage_core);

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
