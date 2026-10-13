/**
 * @file download_writer_thd.cc
 * @author Zuoru YANG (zryang@cse.cuhk.edu.hk)
 * @brief implement the interfaces of DownloadWriterThd
 * @version 0.1
 * @date 2022-07-26
 *
 * @copyright Copyright (c) 2022
 *
 */

#include "../../include/client/download_writer_thd.h"

/**
 * @brief Construct a new DownloadWriterThd object
 *
 * @param file_name download file name
 * @param client_id client ID for isolation
 */
DownloadWriterThd::DownloadWriterThd(string file_name, uint32_t client_id) {
    client_id_ = client_id;
    string real_file_name = file_name + "-d-" + to_string(client_id);
    progress_file_path_ = file_name + "-d-" + to_string(client_id) + ".progress";

    memset(&progress_, 0, sizeof(DownloadProgress_t));

    if (tool::FileExist(progress_file_path_)) {
        LoadProgress();
        // 安全检查：进度文件存在但输出文件丢失时，重新开始下载
        if (!tool::FileExist(real_file_name)) {
            tool::Logging(my_name_.c_str(), "progress file exists but output file missing, "
                "starting fresh download.\n");
            memset(&progress_, 0, sizeof(DownloadProgress_t));
            download_file_hdl_ = fopen(real_file_name.c_str(), "wb");
        } else {
            download_file_hdl_ = fopen(real_file_name.c_str(), "ab");
        }
        if (!download_file_hdl_) {
            tool::Logging(my_name_.c_str(), "cannot open download file: %s\n",
                real_file_name.c_str());
            exit(EXIT_FAILURE);
        }
        if (progress_.received_chunks > 0) {
            tool::Logging(my_name_.c_str(), "resume download: %lu/%lu chunks already done, "
                "%lu bytes written.\n", progress_.received_chunks, progress_.total_chunks,
                progress_.received_bytes);
        }
    } else {
        download_file_hdl_ = fopen(real_file_name.c_str(), "wb");
        if (!download_file_hdl_) {
            tool::Logging(my_name_.c_str(), "cannot open download file: %s\n",
                real_file_name.c_str());
            exit(EXIT_FAILURE);
        }
    }
}

DownloadWriterThd::~DownloadWriterThd() {
    fprintf(stderr, "========DownloadWriterThd Info========\n");
    fprintf(stderr, "write chunk num: %lu\n", _total_write_chunk_num);
    fprintf(stderr, "write data size: %lu\n", _total_write_data_size);
    fprintf(stderr, "======================================\n");
}

/**
 * @brief get the start chunk index for resume download
 *
 * @return uint64_t the chunk index to resume from
 */
uint64_t DownloadWriterThd::GetStartChunkIdx() {
    return progress_.received_chunks;
}

/**
 * @brief set the file recipe head for progress tracking
 *
 * @param head the recipe head from server
 * @param file_name_hash the file name hash for progress identity
 */
void DownloadWriterThd::SetFileRecipeHead(FileRecipeHead_t* head,
    uint8_t* file_name_hash) {
    if (progress_.total_chunks == 0) {
        progress_.total_chunks = head->chunk_num;
        memcpy(progress_.file_name_hash, file_name_hash, CHUNK_HASH_SIZE);
        SaveProgress();
    }
}

/**
 * @brief load download progress from file
 *
 */
void DownloadWriterThd::LoadProgress() {
    ifstream pfile(progress_file_path_, ios::binary);
    if (!pfile.is_open()) {
        tool::Logging(my_name_.c_str(), "cannot open progress file, "
            "starting fresh download.\n");
        memset(&progress_, 0, sizeof(DownloadProgress_t));
        return;
    }
    pfile.read((char*)&progress_, sizeof(DownloadProgress_t));
    pfile.close();
}

/**
 * @brief save download progress to file
 *
 */
void DownloadWriterThd::SaveProgress() {
    ofstream pfile(progress_file_path_, ios::trunc | ios::binary);
    if (!pfile.is_open()) {
        tool::Logging(my_name_.c_str(), "cannot save progress file.\n");
        return;
    }
    pfile.write((char*)&progress_, sizeof(DownloadProgress_t));
    pfile.close();
}

/**
 * @brief delete progress file after successful download
 *
 */
void DownloadWriterThd::DeleteProgress() {
    if (tool::FileExist(progress_file_path_)) {
        remove(progress_file_path_.c_str());
    }
}

/**
 * @brief the main process
 *
 * @param input_MQ input MQ
 */
void DownloadWriterThd::Run(AbsMQ<SendChunk_t>* input_MQ) {
    tool::Logging(my_name_.c_str(), "the main thread is running.\n");

    struct timeval stime;
    struct timeval etime;
    double total_running_time = 0;

    gettimeofday(&stime, NULL);
    // -------- main process --------

    SendChunk_t tmp_data;
    while (true) {
        // extract a chunk from the MQ
        if (input_MQ->_done && input_MQ->IsEmpty()) {
            tool::Logging(my_name_.c_str(), "no chunk in the MQ, all jobs are done.\n");
            break;
        }

        if (input_MQ->Pop(tmp_data)) {
            // write the data to the file
            fwrite((char*)tmp_data.data, tmp_data.header.size, 1,
                download_file_hdl_);
            _total_write_chunk_num++;
            _total_write_data_size += tmp_data.header.size;

            // update progress
            progress_.received_chunks++;
            progress_.received_bytes += tmp_data.header.size;

            // periodically save progress
            if ((progress_.received_chunks % progress_save_interval_) == 0) {
                SaveProgress();
            }
        }
    }

    // final progress save before closing
    SaveProgress();

    // ensure all data is written to the disk
    fsync(fileno(download_file_hdl_));
    fclose(download_file_hdl_);

    // download complete, remove progress file
    DeleteProgress();

    gettimeofday(&etime, NULL);
    total_running_time += tool::GetTimeDiff(stime, etime);

    tool::Logging(my_name_.c_str(), "thread exits, total running time: %lf\n",
        total_running_time);

    return ;
}
