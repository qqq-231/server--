/**
 * @file data_writer_thd.cc
 * @author Zuoru YANG (zryang@cse.cuhk.edu.hk)
 * @brief implement the interfaces of DataWriterThd 
 * @version 0.1
 * @date 2022-07-22
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include "../../include/server/data_writer_thd.h"
#include "../../include/server/cache_comp_thd.h"

boost::mutex DataWriterThd::compressed_index_lck_;

/**
 * @brief Construct a new DataWriterThd object
 * 
 * @param fp_2_addr_db fp to chunk addr index
 * @param feature_2_fp_db feature to fp index
 * @param storage_core storage core
 */
DataWriterThd::DataWriterThd(AbsDatabase* fp_2_addr_db,
    AbsDatabase* compressed_feature_2_fp_db, StorageCore* storage_core,
    CacheCompThd* cache_comp_thd) {
    fp_2_addr_db_ = fp_2_addr_db;
    compressed_feature_2_fp_db_ = compressed_feature_2_fp_db;
    storage_core_ = storage_core;
    cache_comp_thd_ = cache_comp_thd;
    delta_comp_ = new DeltaComp();
    similar_policy_ = new SimilarPolicy();
}

/**
 * @brief Destroy the DataWriterThd object
 * 
 */
DataWriterThd::~DataWriterThd() {
    fprintf(stderr, "\n========== Deferred Representation Choice ==========\n");
    fprintf(stderr, "compared chunks:              %lu\n", deferred_compare_num_);
    fprintf(stderr, "local / full / comp-delta:    %lu / %lu / %lu\n",
        deferred_choose_local_num_, deferred_choose_full_num_,
        deferred_choose_comp_delta_num_);
    fprintf(stderr, "bytes saved versus local:     %lu\n",
        deferred_saved_vs_local_);
    fprintf(stderr, "====================================================\n\n");
    delete delta_comp_;
    delete similar_policy_;
}

/**
 * @brief the main process
 * 
 * @param cur_client current client var
 */
void DataWriterThd::Run(ClientVar* cur_client) {
    tool::Logging(my_name_.c_str(), "the main thread is running.\n");
    AbsMQ<WrappedChunk_t>* input_MQ = cur_client->_comp_2_writer_mq;

    struct timeval stime;
    struct timeval etime;
    double total_running_time = 0;

    struct timeval proc_stime;
    struct timeval proc_etime;
    double total_proc_time = 0;

    gettimeofday(&stime, NULL);
    // -------- main process --------
    WrappedChunk_t tmp_data;
    while (true) {
        // extract a chunk from the MQ // 检查是否完成
        if (input_MQ->_done && input_MQ->IsEmpty()) {
            tool::Logging(my_name_.c_str(), "no chunk in the MQ, all jobs are done.\n");
            break;
        }

        if (input_MQ->Pop(tmp_data)) {
            // check the base chunk // 处理数据块
            gettimeofday(&proc_stime, NULL);
            bool index_compressed_base = false;
            switch (tmp_data.info.stat) {
                case CACHE_DELTA_CHUNK: {
                    this->ProcCacheDeltaChunk(&tmp_data, cur_client);
                    break;
                }
                case DEFERRED_LOCAL_DELTA_CHUNK: {
                    this->ProcCacheDeltaChunk(&tmp_data, cur_client);
                    cache_comp_thd_->RecordDeferredLocal(
                        tmp_data.info.logical_size, tmp_data.info.size);
                    break;
                }
                case COMPARE_RECOMPRESS_CHUNK: {
                    index_compressed_base = this->ProcDeferredComparison(
                        &tmp_data, cur_client);
                    break;
                }
                case UNIQUE_CHUNK: {
                    // Recompress responses carry uncompressed-ciphertext
                    // features, so keep them out of the compressed domain.
                    this->ProcNonSimilarChunk(&tmp_data, cur_client);
                    break;
                }
                case UNIQUE_CHUNK_AFTER_CACHE: {
                    // The payload and features are both from the compressed-
                    // ciphertext representation. Only full COMP_BASE chunks
                    // are published as future bases, preventing delta chains.
                    if (!this->ProcCompressedSimilarChunk(&tmp_data,
                        cur_client)) {
                        this->ProcNonSimilarChunk(&tmp_data, cur_client);
                        index_compressed_base = true;
                    }
                    break;
                }
                case UNCOMP_BASE_CHUNK: {
                    // fallback from recompress: uncompressed encrypted data
                    // store as uncompressed base (download: decrypt only, no decompress)
                    this->ProcNonSimilarChunk(&tmp_data, cur_client);
                    tmp_data.info.addr.stat = UNCOMP_BASE_CHUNK;
                    break;
                }
                default: {
                    tool::Logging(my_name_.c_str(),
                        "wrong chunk type after cache.\n");
                    exit(EXIT_FAILURE);
                }
            }

            // update the fp index   指纹-地址数据库更新
            fp_2_addr_db_->InsertBothBuffer((char*)tmp_data.info.fp, CHUNK_HASH_SIZE,
                (char*)&tmp_data.info.addr, sizeof(KeyForChunkHashDB_t));

            // Publish the feature mapping only after the full base address is
            // durable in fp_2_addr_db_. COMP_DELTA chunks never reach here.
            if (index_compressed_base) {
                boost::mutex::scoped_lock lock(compressed_index_lck_);
                similar_policy_->UpdateFeatureIndex(
                    compressed_feature_2_fp_db_, tmp_data.info.features,
                    tmp_data.info.fp);
            }
            
            gettimeofday(&proc_etime, NULL);
            total_proc_time += tool::GetTimeDiff(proc_stime, proc_etime);
        }
    }

    // check the tail container 尾容器处理  作用：确保所有数据都被保存。
    if (cur_client->_cur_container.cur_size != 0) {
        storage_core_->SaveContainer(&cur_client->_cur_container);
    }

    gettimeofday(&etime, NULL);
    total_running_time += tool::GetTimeDiff(stime, etime);

    tool::Logging(my_name_.c_str(), "thread exits, total proc time: %lf, "
        "total running time: %lf\n", total_proc_time, total_running_time);

    return ;
}

bool DataWriterThd::FindBestCompressedDelta(WrappedChunk_t* input_chunk,
    ClientVar* cur_client, CompressedDeltaResult& result) {
    result = CompressedDeltaResult();
    vector<string> candidates;
    {
        boost::mutex::scoped_lock lock(compressed_index_lck_);
        similar_policy_->GetBaseCandidates(compressed_feature_2_fp_db_,
            input_chunk->info.features, candidates);
    }
    if (candidates.empty()) return false;

    uint8_t base_chunk[ENC_MAX_CHUNK_SIZE];
    uint8_t delta_chunk[ENC_MAX_CHUNK_SIZE];
    uint32_t best_delta_size = input_chunk->info.size;
    vector<string> invalid_candidates;

    for (const auto& candidate : candidates) {
        string base_addr_str;
        if (!fp_2_addr_db_->QueryBuffer(candidate.data(), CHUNK_HASH_SIZE,
            base_addr_str) || base_addr_str.size() < sizeof(KeyForChunkHashDB_t)) {
            invalid_candidates.push_back(candidate);
            continue;
        }

        KeyForChunkHashDB_t base_addr;
        memcpy(&base_addr, base_addr_str.data(), sizeof(base_addr));
        if (base_addr.stat != COMP_BASE_CHUNK) {
            invalid_candidates.push_back(candidate);
            continue;
        }

        // A base in another client's not-yet-flushed tail container is
        // temporarily unavailable. Skip it without deleting its valid index.
        if (!storage_core_->ReadChunk(&base_addr, base_chunk, cur_client)) {
            continue;
        }

#ifdef EDR_BREAKDOWN
        struct timeval delta_stime;
        struct timeval delta_etime;
        gettimeofday(&delta_stime, NULL);
#endif
        uint32_t delta_size = delta_comp_->DeltaEncode(base_chunk,
            base_addr.len, input_chunk->data, input_chunk->info.size,
            delta_chunk);
#ifdef EDR_BREAKDOWN
        gettimeofday(&delta_etime, NULL);
        _total_comp_delta_time += tool::GetTimeDiff(delta_stime, delta_etime);
        _total_comp_delta_data_size += input_chunk->info.size;
#endif
        if (delta_size > 0 && delta_size < best_delta_size) {
            best_delta_size = delta_size;
            result.base_fp = candidate;
            result.size = delta_size;
            result.data.assign(delta_chunk, delta_chunk + delta_size);
        }
    }

    for (const auto& candidate : invalid_candidates) {
        this->PruneCompressedCandidate(input_chunk->info.features, candidate);
    }

    return !result.base_fp.empty();
}

void DataWriterThd::WriteCompressedDelta(WrappedChunk_t* input_chunk,
    CompressedDeltaResult& result, ClientVar* cur_client) {
    memcpy(input_chunk->info.addr.base_fp, result.base_fp.data(),
        CHUNK_HASH_SIZE);
    storage_core_->WriteChunk(&input_chunk->info.addr, result.data.data(),
        result.size, cur_client);
    input_chunk->info.addr.stat = COMP_DELTA_CHUNK;
}

void DataWriterThd::RecordGlobalDelta(uint32_t input_size,
    uint32_t delta_size) {
    double similarity = 1.0 - (double)delta_size / (double)input_size;
    boost::mutex::scoped_lock lock(stat_lck_);
    _total_similar_chunk_num++;
    _total_similar_data_size += input_size;
    _total_delta_size += delta_size;
    if (similarity < 0.4) _similarity_hist[0]++;
    else if (similarity < 0.5) _similarity_hist[1]++;
    else if (similarity < 0.6) _similarity_hist[2]++;
    else if (similarity < 0.7) _similarity_hist[3]++;
    else if (similarity < 0.8) _similarity_hist[4]++;
    else if (similarity < 0.9) _similarity_hist[5]++;
    else _similarity_hist[6]++;
}

bool DataWriterThd::ProcCompressedSimilarChunk(WrappedChunk_t* input_chunk,
    ClientVar* cur_client) {
    CompressedDeltaResult result;
    if (!FindBestCompressedDelta(input_chunk, cur_client, result)) {
        return false;
    }
    double similarity = 1.0 - (double)result.size /
        (double)input_chunk->info.size;
    if (similarity < SIMILARITY_THRESHOLD) return false;

    WriteCompressedDelta(input_chunk, result, cur_client);
    RecordGlobalDelta(input_chunk->info.size, result.size);
    return true;
}

bool DataWriterThd::ProcDeferredComparison(WrappedChunk_t* input_chunk,
    ClientVar* cur_client) {
    string fp_key((char*)input_chunk->info.compressed_fp, CHUNK_HASH_SIZE);
    WrappedChunk_t local_delta;
    bool found_local = false;
    {
        lock_guard<mutex> lock(cur_client->_recompress_map_lck);
        auto it = cur_client->_pending_local_delta_map.find(fp_key);
        if (it != cur_client->_pending_local_delta_map.end()) {
            local_delta = it->second;
            cur_client->_pending_local_delta_map.erase(it);
            found_local = true;
        }
    }

    if (!found_local) {
        // A malformed or duplicate response must never produce an orphan
        // Delta. The compressed payload is independently recoverable.
        ProcNonSimilarChunk(input_chunk, cur_client);
        return true;
    }

    CompressedDeltaResult compressed_delta;
    bool has_compressed_delta = FindBestCompressedDelta(input_chunk,
        cur_client, compressed_delta);

    enum Choice { LOCAL_DELTA, FULL_COMPRESSED, COMPRESSED_DELTA };
    Choice choice = LOCAL_DELTA;
    uint32_t selected_size = local_delta.info.size;
    if (input_chunk->info.size < selected_size) {
        choice = FULL_COMPRESSED;
        selected_size = input_chunk->info.size;
    }
    if (has_compressed_delta && compressed_delta.size < selected_size) {
        choice = COMPRESSED_DELTA;
        selected_size = compressed_delta.size;
    }

    {
        boost::mutex::scoped_lock lock(stat_lck_);
        deferred_compare_num_++;
        if (local_delta.info.size > selected_size) {
            deferred_saved_vs_local_ += local_delta.info.size - selected_size;
        }
        if (choice == LOCAL_DELTA) deferred_choose_local_num_++;
        else if (choice == FULL_COMPRESSED) deferred_choose_full_num_++;
        else deferred_choose_comp_delta_num_++;
    }

    if (choice == LOCAL_DELTA) {
        local_delta.info.stat = CACHE_DELTA_CHUNK;
        ProcCacheDeltaChunk(&local_delta, cur_client);
        input_chunk->info.addr = local_delta.info.addr;
        cache_comp_thd_->RecordDeferredLocal(local_delta.info.logical_size,
            local_delta.info.size);
        return false;
    }

    cache_comp_thd_->RecordDeferredReject();
    if (choice == COMPRESSED_DELTA) {
        WriteCompressedDelta(input_chunk, compressed_delta, cur_client);
        RecordGlobalDelta(input_chunk->info.size, compressed_delta.size);
        return false;
    }

    ProcNonSimilarChunk(input_chunk, cur_client);
    return true;
}

void DataWriterThd::PruneCompressedCandidate(uint64_t* features,
    const string& base_fp) {
    boost::mutex::scoped_lock lock(compressed_index_lck_);
    for (size_t i = 0; i < SUPER_FEATURE_PER_CHUNK; i++) {
        if (similar_policy_->FeatureContainsBase(compressed_feature_2_fp_db_,
            features[i], base_fp)) {
            similar_policy_->RemoveFeatureIndex(compressed_feature_2_fp_db_,
                features[i], base_fp);
        }
    }
}

/**
 * @brief process a non-similar chunk
 * 
 * @param input_chunk input chunk
 * @param cur_client current client
 */
void DataWriterThd::ProcNonSimilarChunk(WrappedChunk_t* input_chunk,
    ClientVar* cur_client) {
   // 存储原始数据
    storage_core_->WriteChunk(&input_chunk->info.addr, input_chunk->data,
        input_chunk->info.size, cur_client);
    input_chunk->info.addr.stat = COMP_BASE_CHUNK;// 标记为全局基准块
    return ;
}

/**
 * @brief process a cache delta chunk
 * 
 * @param input_chunk input chunk
 * @param cur_client current client
 */
void DataWriterThd::ProcCacheDeltaChunk(WrappedChunk_t* input_chunk,
    ClientVar* cur_client) {
    storage_core_->WriteChunk(&input_chunk->info.addr, input_chunk->data,
        input_chunk->info.size, cur_client);
    input_chunk->info.addr.stat = CACHE_DELTA_CHUNK;
    return ;
}

