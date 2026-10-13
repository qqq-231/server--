/**
 * @file download_writer_thd.h
 * @author Zuoru YANG (zryang@cse.cuhk.edu.hk)
 * @brief define the interfaces of DownloadWriterThd
 * @version 0.1
 * @date 2022-07-26
 *
 * @copyright Copyright (c) 2022
 *
 */

#ifndef EDRSTORE_DOWNLOAD_WRITER_THD_H
#define EDRSTORE_DOWNLOAD_WRITER_THD_H

#include "../configure.h"
#include "../message_queue/mq_factory.h"
#include "../data_structure.h"

class DownloadWriterThd {
    private:
        string my_name_ = "DownloadWriterThd";

        // download file
        FILE* download_file_hdl_;

        // 断点续传
        uint32_t client_id_;
        string progress_file_path_;
        DownloadProgress_t progress_;
        uint64_t progress_save_interval_ = 512;  // 每512个chunk保存一次进度

        void LoadProgress();
        void SaveProgress();
        void DeleteProgress();

    public:
        uint64_t _total_write_data_size = 0;
        uint64_t _total_write_chunk_num = 0;

        /**
         * @brief Construct a new DownloadWriterThd object
         *
         * @param file_name download file name
         * @param client_id client ID for isolation
         */
        DownloadWriterThd(string file_name, uint32_t client_id);

        /**
         * @brief Destroy the DownloadWriterThd object
         *
         */
        ~DownloadWriterThd();

        /**
         * @brief the main process
         *
         * @param input_MQ input MQ
         */
        void Run(AbsMQ<SendChunk_t>* input_MQ);

        /**
         * @brief get the start chunk index for resume download
         *
         * @return uint64_t the chunk index to resume from
         */
        uint64_t GetStartChunkIdx();

        /**
         * @brief set the file recipe head (total chunks) for progress tracking
         *
         * @param head the recipe head from server
         * @param file_name_hash the file name hash for progress identity
         */
        void SetFileRecipeHead(FileRecipeHead_t* head, uint8_t* file_name_hash);
};

#endif
