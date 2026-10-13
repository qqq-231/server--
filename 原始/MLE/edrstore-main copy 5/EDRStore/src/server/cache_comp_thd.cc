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

/**
 * @brief the main process
 * 
 * @param cur_client current client var
 */
void CacheCompThd::Run(ClientVar* cur_client) {
    tool::Logging(my_name_.c_str(), "the main thread is running.\n");
    AbsMQ<WrappedChunk_t>* input_MQ = cur_client->_dual_2_comp_mq;
    AbsMQ<WrappedChunk_t>* output_MQ = cur_client->_comp_2_writer_mq;
    InformCache* inform_cache = cur_client->_inform_cache;

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
                    // previous chunk is cached chunk
                    input_data.info.stat = UNIQUE_CHUNK;// 状态转换
                    output_MQ->Push(input_data);//直接推送队列
                    break;
                }
                case UNIQUE_CHUNK: {
                    // previous cache is not a insert cache chunk, check cache
                        // 之前的缓存不是插入缓存块，检查缓存
#ifdef EDR_BREAKDOWN
                    gettimeofday(&_cache_delta_stime, NULL);
#endif
                        // 在缓存中查找相似块
                    uint64_t similarity_search_time_ns = 0;
                    uint64_t delta_compression_time_ns = 0;
                    bool is_similar = inform_cache->ProcessNormalChunk(&input_data,
                        &output_data, &similarity_search_time_ns,
                        &delta_compression_time_ns);
                    _performance_similarity_search_time_ns.fetch_add(
                        similarity_search_time_ns, std::memory_order_relaxed);
                    _performance_delta_compression_time_ns.fetch_add(
                        delta_compression_time_ns, std::memory_order_relaxed);

#ifdef EDR_BREAKDOWN
                    gettimeofday(&_cache_delta_etime, NULL);
                    _total_cache_delta_time += tool::GetTimeDiff(_cache_delta_stime,
                        _cache_delta_etime);
                    _total_cache_delta_data_size += input_data.info.size;
#endif

                    if(is_similar) {
                        // similar chunk // 找到相似块，进行增量压缩
                        output_MQ->Push(output_data); // 推送增量数据
                        _total_similar_chunk_num++;// 更新统计
                        _total_similar_data_size += input_data.info.size;
                        _total_delta_size += output_data.info.size;
                        double similarity = 1.0 - (double)output_data.info.size / (double)input_data.info.size;
                        if (similarity < 0.4) _similarity_hist[0]++;
                        else if (similarity < 0.5) _similarity_hist[1]++;
                        else if (similarity < 0.6) _similarity_hist[2]++;
                        else if (similarity < 0.7) _similarity_hist[3]++;
                        else if (similarity < 0.8) _similarity_hist[4]++;
                        else if (similarity < 0.9) _similarity_hist[5]++;
                        else _similarity_hist[6]++;
                    } else {
                        // non-similar chunk, back to original stat
                        if (inform_cache->GetCacheSize() == 0) {
                            // 缓存为空，直接推送原始块
                            input_data.info.stat = UNIQUE_CHUNK;
                            output_MQ->Push(input_data);
                        } else {
                            // 缓存非空但没有找到相似块，这是异常情况
                            tool::Logging(my_name_.c_str(), "wrong chunk type in cache.\n");
                            exit(EXIT_FAILURE);
                        }
                    }

// #ifdef EDR_BREAKDOWN
//                     gettimeofday(&_cache_delta_etime, NULL);
//                     _total_cache_delta_time += tool::GetTimeDiff(_cache_delta_stime,
//                         _cache_delta_etime);
//                     _total_cache_delta_data_size += input_data.info.size;
// #endif
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
                    inform_cache->InsertCachedChunk(&input_data);

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
                    inform_cache->EvictCacheChunk(&input_data);

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

    output_MQ->_done = true;
    gettimeofday(&etime, NULL);
    total_running_time += tool::GetTimeDiff(stime, etime);

    tool::Logging(my_name_.c_str(), "thread exits, total proc time: %lf, "
        "total running time: %lf\n", total_proc_time, total_running_time);

    return ;
}
