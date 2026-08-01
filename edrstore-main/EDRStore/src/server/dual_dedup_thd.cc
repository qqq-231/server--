/**
 * @file dual_dedup_thd.cc
 * @author Jia Zhao
 * @brief implement the interfaces of DualDedupThd
 * @version 0.1
 * @date 2022-12-28
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include "../../include/server/dual_dedup_thd.h"

DualDedupThd::DualDedupThd(AbsDatabase* fp_2_addr_db) {
    fp_2_addr_db_ = fp_2_addr_db;
    dedup_util_ = new DedupDetect(fp_2_addr_db_);
    finesse_util_ = new FinesseUtil(SUPER_FEATURE_PER_CHUNK,
    FEATURE_PER_CHUNK, FEATURE_PER_SUPER_FEATURE);
}

DualDedupThd::~DualDedupThd() {
    delete dedup_util_;
    delete finesse_util_;
}

/**
 * @brief the main process
 * 
 * @param cur_client current client var
 */
void DualDedupThd::Run(ClientVar* cur_client) {
    tool::Logging(my_name_.c_str(), "the main thread is running.\n");
    //// 获取输入输出队列
    AbsMQ<WrappedChunk_t>* input_MQ = cur_client->_recv_2_dual_mq;// 从DataRecvThd来
    AbsMQ<WrappedChunk_t>* output_MQ = cur_client->_dual_2_comp_mq;// 到CacheCompThd去

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

    while(true) {
        // extract a chunk from the MQ   // 检查是否完成
        if (input_MQ->_done && input_MQ->IsEmpty()) {
            tool::Logging(my_name_.c_str(), "no chunk in the MQ, all jobs are done.\n");
            break;
        }



        // 从队列中取出数据块
        // 根据数据块状态进行处理
        // 处理不同类型的块
        if(input_MQ->Pop(input_data)) {
            switch (input_data.info.stat) {
                case CHUNK_PAIR: {
                    // perform deduplication   执行去重检测
#ifdef EDR_BREAKDOWN
                gettimeofday(&_dedup_stime, NULL);
#endif
                        // 1. 执行去重检测
                    dedup_util_->DetectDuplicate(&input_data.info);
#ifdef EDR_BREAKDOWN
                gettimeofday(&_dedup_etime, NULL);
                _total_dedup_time += tool::GetTimeDiff(_dedup_stime, _dedup_etime);
#endif
                        // 2. 如果是唯一块，进一步处理
                    if(input_data.info.stat == UNIQUE_CHUNK) {
                        input_data.info.stat = UNIQUE_CHUNK_AFTER_CACHE;// 标记为缓存后的唯一块
#ifdef EDR_BREAKDOWN
                    gettimeofday(&_cipher_feature_stime, NULL);
#endif
                        // compute the feature here   // 3. 提取特征（用于后续的相似性匹配）
                        finesse_util_->ExtractFeature(cur_client->_rabin_ctx,
                            input_data.data, input_data.info.size,
                            input_data.info.features);
#ifdef EDR_BREAKDOWN
                    gettimeofday(&_cipher_feature_etime, NULL);
                    _total_cipher_feature_time += tool::GetTimeDiff(
                        _cipher_feature_stime, _cipher_feature_etime);
                    _total_cipher_feature_data_size += input_data.info.size;
#endif
                        // 4. 推送到下一阶段
                        output_MQ->Push(input_data);

                        // update stat
                        _total_unique_chunk_num++;
                        _total_unique_data_size += input_data.info.size;
                    }
                    
                    break;
                }
                case SINGLE_CHUNK: {
                    // perform deduplication    执行去重检测
#ifdef EDR_BREAKDOWN
                gettimeofday(&_dedup_stime, NULL);
#endif
                        // 1. 执行去重检测
                    dedup_util_->DetectDuplicate(&input_data.info);
#ifdef EDR_BREAKDOWN
                gettimeofday(&_dedup_etime, NULL);
                _total_dedup_time += tool::GetTimeDiff(_dedup_stime, _dedup_etime);
#endif

                        // 2. 如果是唯一块，推送到下一阶段
                    if(input_data.info.stat == UNIQUE_CHUNK) {
                        output_MQ->Push(input_data);
                        // update stat // 更新统计
                        _total_unique_chunk_num++;
                        _total_unique_data_size += input_data.info.size;
                    }

                    break;
                }
                case CACHE_INSERT_CHUNK: {
                    // directly pass to the next thd   // 直接传递给下一阶段（CacheCompThd）
                    output_MQ->Push(input_data);
                    break;
                }
                case CACHE_EVICT_CHUNK: {
                    // directly pass to the next thd   // 直接传递给下一阶段（CacheCompThd）
                    output_MQ->Push(input_data);
                    break;
                }
                default: {
                    tool::Logging(my_name_.c_str(), "wrong chunk input type.\n");
                    exit(EXIT_FAILURE);
                }

            }

            // output_MQ->Push(input_data);
        }
    }

    output_MQ->_done = true;

    gettimeofday(&etime, NULL);
    total_running_time += tool::GetTimeDiff(stime, etime);

    tool::Logging(my_name_.c_str(), "thread exits, total proc time: %lf, "
        "total running time: %lf\n", total_proc_time, total_running_time);
    
    return;

}