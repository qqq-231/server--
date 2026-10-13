/**
 * @file plain_similar_thd.cc
 * @author Zuoru YANG (zryang@cse.cuhk.edu.hk)
 * @brief implement the interface of PlainSimilarThd
 * @version 0.1
 * @date 2022-06-10
 *
 * @copyright Copyright (c) 2022
 *
 */

#include "../../include/client/plain_similar_thd.h"
#include "../../include/util/performance_stats.h"

#include "../../include/chunker/odess_start.h"

/**
 * @brief Construct a new PlainSimilarThd object
 *
 */
PlainSimilarThd::PlainSimilarThd() {

if (feature=="finesse")
{//使用finesse
//==================  finesse  ==========================
     rabin_util_ = new RabinFPUtil(config.GetSimilarSlidingWinSize());
     finesse_util_ = new FinesseUtil(SUPER_FEATURE_PER_CHUNK,
         FEATURE_PER_CHUNK, FEATURE_PER_SUPER_FEATURE);
    rabin_util_->NewCtx(rabin_ctx_);
} else if (feature=="n_transform")
{//使用n_transform (Rabin-based)
     rabin_util_ = new RabinFPUtil(config.GetSimilarSlidingWinSize());
     n_transform = new NTransform(SUPER_FEATURE_PER_CHUNK,
         FEATURE_PER_CHUNK, FEATURE_PER_SUPER_FEATURE);
     rabin_util_->NewCtx(rabin_ctx_);
}
else
{
    gear_util_ = new GearHashUtil(config.GetSimilarSlidingWinSize());
    gear_util_->NewCtx(gear_ctx_);
    if (feature=="odess_no")
    {//不聚合
        odess_simple_ = new OdessSimple(SUPER_FEATURE_PER_CHUNK,
               FEATURE_PER_CHUNK, FEATURE_PER_SUPER_FEATURE);

    }
    else if (feature=="odess_no_mask")
    {//不聚合+no mask


    } else if (feature=="odess")
    {//聚合
          odess_util_ = new OdessUtil(SUPER_FEATURE_PER_CHUNK,
        FEATURE_PER_CHUNK, FEATURE_PER_SUPER_FEATURE);

    } else if (feature=="odess_start")
    {//合成超特征---原始

        odess_start_= new OdessStart(SUPER_FEATURE_PER_CHUNK,
                FEATURE_PER_CHUNK, FEATURE_PER_SUPER_FEATURE);

    } else if (feature=="newminhash")
    {//
         new_minhash_ = new New_MinHash(SUPER_FEATURE_PER_CHUNK,
         FEATURE_PER_CHUNK, FEATURE_PER_SUPER_FEATURE);

    } else if (feature=="n_transform_gear")
    {//
        n_transform_gear = new NTransform_gear(SUPER_FEATURE_PER_CHUNK,
         FEATURE_PER_CHUNK, FEATURE_PER_SUPER_FEATURE);

    }



}

}

/**
 * @brief Destroy the PlainSimilarThd object
 *
 */
PlainSimilarThd::~PlainSimilarThd() {

    if (gear_util_) {
        gear_util_->FreeCtx(gear_ctx_);
        delete gear_util_;
    }

    delete odess_util_;
    delete odess_simple_;
    delete n_transform;
    delete n_transform_gear;
    delete new_minhash_;
    delete odess_start_;
    //============  rabin  finess  =======================
    if (rabin_util_) {
        rabin_util_->FreeCtx(rabin_ctx_);
        delete rabin_util_;
    }
    delete finesse_util_;
}

/**
 * @brief the main thread
 *
 * @param input_MQ the input MQ
 * @param output_MQ the output MQ
 */
void PlainSimilarThd::Run(AbsMQ<Chunk_t>* input_MQ,
    AbsMQ<FeatureChunk_t>* output_MQ) {
    tool::Logging(my_name_.c_str(), "the main thread is running.\n");

    struct timeval stime;
    struct timeval etime;
    double total_running_time = 0;

    gettimeofday(&stime, NULL);
    // -------- main process --------

    FeatureChunk_t tmp_data;
    while (true) {
        if (input_MQ->_done && input_MQ->IsEmpty()) {
            tool::Logging(my_name_.c_str(), "no chunk in the MQ, all jobs are done.\n");
            break;
        }

        if (input_MQ->Pop(tmp_data.chunk)) {
            // extract a chunk from the MQ
            const auto performance_feature_start = PerformanceStats::Clock::now();
#ifdef EDR_BREAKDOWN
            gettimeofday(&_plain_feature_stime, NULL);
#endif

            switch(tmp_data.chunk.type) {
                case NORMAL_CHUNK: {

                        if (feature=="finesse")
                        {//使用finesse
                            //==================  finesse  ==========================
                            finesse_util_->ExtractFeature(rabin_ctx_, tmp_data.chunk.raw_chunk.data,
                                tmp_data.chunk.raw_chunk.size, tmp_data.features);

                        }
                        else
                        {
                            if (feature=="odess_no")
                            {//不聚合
                                //==================  odess_simple_
                                odess_simple_->ExtractFeature(gear_ctx_, tmp_data.chunk.raw_chunk.data,
                                                      tmp_data.chunk.raw_chunk.size, tmp_data.features);


                            }
                            else if (feature=="odess_no_mask")
                            {//不聚合+no mask

                            } else if (feature=="odess")
                            {//聚合
                                odess_util_->ExtractFeature(gear_ctx_, tmp_data.chunk.raw_chunk.data,
                                                   tmp_data.chunk.raw_chunk.size, tmp_data.features);

                            } else if (feature=="odess_start")
                            {//合成超特征---原始
                                odess_start_->ExtractFeature(gear_ctx_, tmp_data.chunk.raw_chunk.data,
                                tmp_data.chunk.raw_chunk.size, tmp_data.features);


                            } else if (feature=="newminhash")
                            {//
                                new_minhash_->ExtractFeature(gear_ctx_, tmp_data.chunk.raw_chunk.data,
                                                   tmp_data.chunk.raw_chunk.size, tmp_data.features);

                            } else if (feature=="n_transform")
                            {//
                                n_transform->ExtractFeature(rabin_ctx_, tmp_data.chunk.raw_chunk.data,
                                    tmp_data.chunk.raw_chunk.size, tmp_data.features);

                            }
                            else if (feature=="n_transform_gear")
                            {//
                                n_transform_gear->ExtractFeature(gear_ctx_, tmp_data.chunk.raw_chunk.data,
                                    tmp_data.chunk.raw_chunk.size, tmp_data.features);

                            }



                        }
                    break;
                }
                case RECIPE_CHUNK: {
                    break;
                }
                default: {
                    tool::Logging(my_name_.c_str(), "wrong chunk type.\n");
                    exit(EXIT_FAILURE);
                }
            }

            if (tmp_data.chunk.type == NORMAL_CHUNK) {
                _performance_feature_time_ns += PerformanceStats::ElapsedNs(
                    performance_feature_start);
            }

#ifdef EDR_BREAKDOWN
            gettimeofday(&_plain_feature_etime, NULL);
            _total_plain_feature_time += tool::GetTimeDiff(
                _plain_feature_stime, _plain_feature_etime);
            if (tmp_data.chunk.type != RECIPE_CHUNK) {
                _total_plain_feature_size += tmp_data.chunk.raw_chunk.size;
            }
#endif

            // insert the chunk to the output MQ
            output_MQ->Push(tmp_data);
        }
    }

    // set the output MQ done
    output_MQ->_done = true;
    gettimeofday(&etime, NULL);
    total_running_time += tool::GetTimeDiff(stime, etime);

    tool::Logging(feature.c_str(),"PlainSimilarThd\n");
    tool::Logging(my_name_.c_str(), "thread exits, total running time: %lf\n",
        total_running_time);

    return ;
}
