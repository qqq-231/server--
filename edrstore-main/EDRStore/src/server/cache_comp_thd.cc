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

void CacheCompThd::RecordSimDist(uint32_t orig_size, uint32_t delta_size) {
    float ratio = 1.0f - (float)delta_size / (float)orig_size;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio < 0.4f) _sim_dist[0]++;
    else if (ratio < 0.5f) _sim_dist[1]++;
    else if (ratio < 0.6f) _sim_dist[2]++;
    else if (ratio < 0.7f) _sim_dist[3]++;
    else if (ratio < 0.8f) _sim_dist[4]++;
    else if (ratio < 0.9f) _sim_dist[5]++;
    else _sim_dist[6]++;
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
                    // CHUNK_PAIR compressed data — check unified InformCache
#ifdef EDR_BREAKDOWN
                    gettimeofday(&_cache_delta_stime, NULL);
#endif

                    bool is_similar = inform_cache->ProcessNormalChunk(&input_data,
                        &output_data);

#ifdef EDR_BREAKDOWN
                    gettimeofday(&_cache_delta_etime, NULL);
                    _total_cache_delta_time += tool::GetTimeDiff(_cache_delta_stime,
                        _cache_delta_etime);
                    _total_cache_delta_data_size += input_data.info.size;
#endif

                    if (is_similar) {
                        output_MQ->Push(output_data);
                        _total_similar_chunk_num++;
                        _total_similar_data_size += input_data.info.size;
                        _total_delta_size += output_data.info.size;
                        RecordSimDist(input_data.info.size, output_data.info.size);
                    } else {
                        // non-similar: insert as future base, then push as unique
                        input_data.info.stat = CACHE_INSERT_CHUNK;
                        inform_cache->InsertCachedChunk(&input_data);
                        input_data.info.stat = UNIQUE_CHUNK;
                        output_MQ->Push(input_data);
                    }
                    break;
                }
                case UNIQUE_CHUNK: {
                    // previous cache is not a insert cache chunk, check cache
                        // 之前的缓存不是插入缓存块，检查缓存
#ifdef EDR_BREAKDOWN
                    gettimeofday(&_cache_delta_stime, NULL);
#endif
                        // 在缓存中查找相似块
                    bool is_similar = inform_cache->ProcessNormalChunk(&input_data,
                        &output_data);

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
                        RecordSimDist(input_data.info.size, output_data.info.size);
                    } else {
                        // non-similar: insert as future base for other chunks
                        input_data.info.stat = CACHE_INSERT_CHUNK;
                        inform_cache->InsertCachedChunk(&input_data);
                        input_data.info.stat = UNIQUE_CHUNK;
                        output_MQ->Push(input_data);
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

    fprintf(stderr, "========相似度区间分布========\n");
    fprintf(stderr, "[0~0.4):  %lu\n", _sim_dist[0]);
    fprintf(stderr, "[0.4~0.5): %lu\n", _sim_dist[1]);
    fprintf(stderr, "[0.5~0.6): %lu\n", _sim_dist[2]);
    fprintf(stderr, "[0.6~0.7): %lu\n", _sim_dist[3]);
    fprintf(stderr, "[0.7~0.8): %lu\n", _sim_dist[4]);
    fprintf(stderr, "[0.8~0.9): %lu\n", _sim_dist[5]);
    fprintf(stderr, "[0.9~1]:   %lu\n", _sim_dist[6]);
    fprintf(stderr, "=============================\n");
    uint64_t sim_dist_total = _sim_dist[0] + _sim_dist[1] + _sim_dist[2] +
        _sim_dist[3] + _sim_dist[4] + _sim_dist[5] + _sim_dist[6];
    fprintf(stderr, "相似块总数: %lu, 分布合计: %lu\n",
        _total_similar_chunk_num, sim_dist_total);

    return ;
}