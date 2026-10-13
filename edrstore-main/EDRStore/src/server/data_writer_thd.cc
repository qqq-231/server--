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
#include "../../include/util/performance_stats.h"

/**
 * @brief Construct a new DataWriterThd object
 * 
 * @param fp_2_addr_db fp to chunk addr index
 * @param storage_core storage core
 * @param binary_tree_index persistent similarity candidate index
 */
DataWriterThd::DataWriterThd(AbsDatabase* fp_2_addr_db,
    StorageCore* storage_core, BinaryTreeIndex* binary_tree_index) {
    fp_2_addr_db_ = fp_2_addr_db;
    storage_core_ = storage_core;
    binary_tree_index_ = binary_tree_index;
    delta_comp_ = new DeltaComp();
    similar_policy_ = new SimilarPolicy();
}

/**
 * @brief Destroy the DataWriterThd object
 * 
 */
DataWriterThd::~DataWriterThd() {
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
            switch (tmp_data.info.stat) {
                case CACHE_DELTA_CHUNK: {
                    this->ProcCacheDeltaChunk(&tmp_data, cur_client);
                    break;
                }
                case UNIQUE_CHUNK: {
                    const auto performance_search_start =
                        PerformanceStats::Clock::now();
                    similar_policy_->FindBaseChunk(binary_tree_index_,
                        fp_2_addr_db_, &tmp_data.info);
                    _performance_similarity_search_time_ns.fetch_add(
                        PerformanceStats::ElapsedNs(performance_search_start),
                        std::memory_order_relaxed);
                    switch (tmp_data.info.stat) {
                        case SIMILAR_CHUNK: {
                            _total_similar_chunk_num++;
                            _total_similar_data_size += tmp_data.info.size;
                            this->ProcSimilarChunk(&tmp_data, cur_client);
                            break;
                        }
                        case NON_SIMILAR_CHUNK: {
                            this->ProcNonSimilarChunk(&tmp_data, cur_client);
                            break;
                        }
                        default: {
                            tool::Logging(my_name_.c_str(),
                                "wrong unique chunk type.\n");
                            exit(EXIT_FAILURE);
                        }
                    }
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

            // Publish a tree entry only after both data and authoritative
            // fp->address metadata are visible.
            binary_tree_index_->InsertChunk(&tmp_data.info);
            
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

/**
 * @brief process a similar chunk
 * 
 * @param input_chunk input chunk
 * @param cur_client current client
 */
void DataWriterThd::ProcSimilarChunk(WrappedChunk_t* input_chunk,
    ClientVar* cur_client) {
    uint8_t base_chunk[ENC_MAX_CHUNK_SIZE];
    uint32_t base_chunk_size = 0;
    uint8_t delta_chunk[ENC_MAX_CHUNK_SIZE];
    uint32_t delta_chunk_size = 0;

    //获取基准块
    base_chunk_size = this->FetchBaseChunk(input_chunk->info.addr.base_fp,
        base_chunk, cur_client);

    if(base_chunk_size == 0){
        // // 避免增量压缩，直接写入原始数据    基准块获取失败
        // 处理基准块不存在时的降级处理
        // 直接存储原始数据，确保数据不丢失
        // avoid delta, directly write
        storage_core_->WriteChunk(&input_chunk->info.addr, input_chunk->data,
        input_chunk->info.size, cur_client);
    }

#ifdef EDR_BREAKDOWN
    gettimeofday(&_comp_delta_stime, NULL);
#endif
    //计算当前块与基准块的差异
    //生成增量编码数据
    //增量编码
    const auto performance_delta_start = PerformanceStats::Clock::now();
    delta_chunk_size = delta_comp_->DeltaEncode(base_chunk, base_chunk_size,
        input_chunk->data, input_chunk->info.size, delta_chunk);
    _performance_delta_compression_time_ns.fetch_add(
        PerformanceStats::ElapsedNs(performance_delta_start),
        std::memory_order_relaxed);
//存储增量数据
    storage_core_->WriteChunk(&input_chunk->info.addr, delta_chunk,
        delta_chunk_size, cur_client);
    input_chunk->info.addr.stat = COMP_DELTA_CHUNK;// 标记为全局增量块

    // update stat
    _total_delta_size += delta_chunk_size;// 增加全局增量数据大小
    double similarity = 1.0 - (double)delta_chunk_size / (double)input_chunk->info.size;
    if (similarity < 0.4) _similarity_hist[0]++;
    else if (similarity < 0.5) _similarity_hist[1]++;
    else if (similarity < 0.6) _similarity_hist[2]++;
    else if (similarity < 0.7) _similarity_hist[3]++;
    else if (similarity < 0.8) _similarity_hist[4]++;
    else if (similarity < 0.9) _similarity_hist[5]++;
    else _similarity_hist[6]++;

#ifdef EDR_BREAKDOWN
    gettimeofday(&_comp_delta_etime, NULL);
    _total_comp_delta_time += tool::GetTimeDiff(_comp_delta_stime,
        _comp_delta_etime);
    _total_comp_delta_data_size += input_chunk->info.size;
#endif

    return ;
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

/**
 * @brief fetch the base chunk
 * 
 * @param base_fp base chunk fp
 * @param base_data base chunk data
 * @param cur_client current client
 * @return uint32_t base chunk size
 */
uint32_t DataWriterThd::FetchBaseChunk(uint8_t* base_fp, uint8_t* base_data,
    ClientVar* cur_client) {
    string base_addr_str;

    // step-1: query the fp index to get the base chunk address  查询 fp 索引以获取基块地址
    // 查询基准块地址
    if (!fp_2_addr_db_->QueryBuffer((char*)base_fp, CHUNK_HASH_SIZE, base_addr_str)) {
        tool::Logging(my_name_.c_str(), "req base chunk not exits.\n");
        exit(EXIT_FAILURE);
    }

    // step-2: read base chunk from the disk    从磁盘读取基本块
    // 读取基准块数据
    KeyForChunkHashDB_t* base_addr = (KeyForChunkHashDB_t*)&base_addr_str[0];
    bool read = storage_core_->ReadChunk(base_addr, base_data, cur_client);
    
    if(read == false){
        return 0;
    }

    return base_addr->len;
}
