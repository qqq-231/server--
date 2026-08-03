/**
 * @file cache_comp_thd.cc
 * @author Zuoru YANG (zryang@cse.cuhk.edu.hk)
 * @brief implement the interfaces of CacheCompThd
 * @version 0.1
 * @date 2022-08-03
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include "../../include/server/cache_comp_thd.h"

/**
 * @brief Construct a new CacheCompThd object
 * 
 */
CacheCompThd::CacheCompThd() {

}

/**
 * @brief Destroy the CacheCompThd object
 * 
 */
CacheCompThd::~CacheCompThd() {

}

void CacheCompThd::RecordDeferredLocal(uint32_t logical_size,
    uint32_t delta_size) {
    boost::mutex::scoped_lock lock(stat_lck_);
    _total_similar_chunk_num++;
    _total_similar_data_size += logical_size;
    _total_delta_size += delta_size;
}

void CacheCompThd::RecordDeferredReject() {
    boost::mutex::scoped_lock lock(stat_lck_);
    _total_similarity_reject_num++;
}

/**
 * @brief the main process
 * 
 * @param cur_client current client var
 */
void CacheCompThd::Run(ClientVar* cur_client) {
    tool::Logging(my_name_.c_str(), "the main thread is running.\n");
    AbsMQ<WrappedChunk_t>* input_MQ = cur_client->_dual_2_comp_mq;
    AbsMQ<WrappedChunk_t>* output_MQ = cur_client->_comp_2_writer_mq;

    struct timeval stime;
    struct timeval etime;
    double total_running_time = 0;

    struct timeval proc_stime;
    struct timeval proc_etime;
    double total_proc_time = 0;

    gettimeofday(&stime, NULL);
    // -------- main process --------
    WrappedChunk_t input_data;
    WrappedChunk_t output_data;
    while (true) {
        // extract a chunk from the MQ
        if (input_MQ->_done && input_MQ->IsEmpty()) {
            tool::Logging(my_name_.c_str(), "no chunk in the MQ, all jobs are done.\n");
            break;
        }

        if (input_MQ->Pop(input_data)) {
            gettimeofday(&proc_stime, NULL);
            switch (input_data.info.stat) {
                case UNIQUE_CHUNK_AFTER_CACHE: {
                    // This payload is already compressed then encrypted. Keep
                    // the representation marker so DataWriterThd can use the
                    // separate compressed-ciphertext similarity index. It must
                    // not enter InformCache's uncompressed-ciphertext index.
                    output_MQ->Push(input_data);
                    break;
                }
                case UNIQUE_CHUNK: {
                    // 两级查找（内存 + LevelDB）合并全部相似性检测
#ifdef EDR_BREAKDOWN
                    gettimeofday(&_cache_delta_stime, NULL);
#endif
                    bool is_similar = inform_cache_.ProcessNormalChunk(&input_data,
                        &output_data, cur_client->_client_id);

#ifdef EDR_BREAKDOWN
                    gettimeofday(&_cache_delta_etime, NULL);
                    _total_cache_delta_time += tool::GetTimeDiff(_cache_delta_stime,
                        _cache_delta_etime);
                    _total_cache_delta_data_size += input_data.info.size;
#endif

                    if(is_similar) {
                        // 先计算相似度（在 push 之前）
                        double similarity = 1.0 - (double)output_data.info.size / (double)input_data.info.size;

                        // 直方图统计：覆盖所有 is_similar 的块（含被拒绝的）
                        {
                            boost::mutex::scoped_lock lock(stat_lck_);
                            if (similarity < 0.4) _similarity_hist[0]++;
                            else if (similarity < 0.5) _similarity_hist[1]++;
                            else if (similarity < 0.6) _similarity_hist[2]++;
                            else if (similarity < 0.7) _similarity_hist[3]++;
                            else if (similarity < 0.8) _similarity_hist[4]++;
                            else if (similarity < 0.9) _similarity_hist[5]++;
                            else _similarity_hist[6]++;
                        }

                        if (similarity < SIMILARITY_THRESHOLD) {
                            // 相似度过低 → 视为非相似块
                            // InformCache 仍存未压缩加密数据（保证 delta 编码质量）
                            inform_cache_.InsertCachedChunk(&input_data,
                                cur_client->_client_id, true);
                            // 容器存储：请求客户端补发加密压缩数据
                            input_data.info.stat = PENDING_RECOMPRESS_CHUNK;
                            cur_client->_recompress_mq->Push(input_data);
                            RecordDeferredReject();
                        } else if (similarity < RECOMPRESS_COMPARE_THRESHOLD) {
                            // Delay the physical write until the compressed
                            // representation arrives. output_data already owns
                            // the best local Delta and its base fingerprint.
                            output_data.info.stat = PENDING_LOCAL_DELTA_CHUNK;
                            cur_client->_recompress_mq->Push(output_data);
                        } else {
                            // 高质量相似块 → 走增量存储
                            output_MQ->Push(output_data);
                            RecordDeferredLocal(input_data.info.size,
                                output_data.info.size);
                        }
                    } else {
                        // 未找到相似块 → InformCache 仍存未压缩加密数据（保证 delta 编码质量）
                        inform_cache_.InsertCachedChunk(&input_data,
                            cur_client->_client_id, true);
                        // 容器存储：请求客户端补发加密压缩数据（避免存储未压缩加密数据）
                        input_data.info.stat = PENDING_RECOMPRESS_CHUNK;
                        cur_client->_recompress_mq->Push(input_data);
                        RecordDeferredReject();
                    }
                    break;
                }
                case CACHE_INSERT_CHUNK: {
                        //这是来自FULL_EDR_CACHE_CHUNK的未压缩缓存块
                        //插入缓存，供后续块进行相似性比较
                        //只更新缓存，不推送到输出队列

#ifdef EDR_BREAKDOWN
                    gettimeofday(&_cache_manage_stime, NULL);
#endif
                        // 将块插入缓存
                    inform_cache_.InsertCachedChunk(&input_data,
                        cur_client->_client_id, false);

#ifdef EDR_BREAKDOWN
                    gettimeofday(&_cache_manage_etime, NULL);
                    _total_cache_manage_time += tool::GetTimeDiff(_cache_manage_stime,
                        _cache_manage_etime);
                    _total_cache_manage_data_size += input_data.info.size;
#endif
                    break;
                }
                case CACHE_EVICT_CHUNK: {
#ifdef EDR_BREAKDOWN
                    gettimeofday(&_cache_manage_stime, NULL);
#endif
                    inform_cache_.EvictCacheChunk(&input_data,
                        cur_client->_client_id);

#ifdef EDR_BREAKDOWN
                    gettimeofday(&_cache_manage_etime, NULL);
                    _total_cache_manage_time += tool::GetTimeDiff(_cache_manage_stime,
                        _cache_manage_etime);
#endif

                    break;
                }
                default: {
                    tool::Logging(my_name_.c_str(), "wrong chunk input type.\n");
                    exit(EXIT_FAILURE);
                }
            }

            gettimeofday(&proc_etime, NULL);
            total_proc_time += tool::GetTimeDiff(proc_stime, proc_etime);
        }
    }

    // _done on output_MQ deferred to DataRecvThd after recompress handling
    cur_client->_recompress_mq->_done = true;
    gettimeofday(&etime, NULL);
    total_running_time += tool::GetTimeDiff(stime, etime);

    tool::Logging(my_name_.c_str(), "thread exits, total proc time: %lf, "
        "total running time: %lf\n", total_proc_time, total_running_time);

    return ;
}
