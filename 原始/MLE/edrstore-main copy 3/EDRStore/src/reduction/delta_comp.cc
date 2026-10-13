/**
 * @file delta_comp.cc
 * @author Zuoru YANG (zryang@cse.cuhk.edu.hk)
 * @brief implement the interfaces of DeltaComp
 * @version 0.1
 * @date 2022-07-22
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include "../../include/reduction/delta_comp.h"

/**
 * @brief Construct a new Delta Comp object
 * 
 * @param storage_core storage core
 */
DeltaComp::DeltaComp() {
}

/**
 * @brief Destroy the Delta Comp object
 * 
 */
DeltaComp::~DeltaComp() {
}

/**
 * @brief perform delta encoding
 * 
 * @param base_chunk base chunk data
 * @param base_size base chunk size
 * @param input_chunk input chunk
 * @param input_size input chunk size
 * @param delta_chunk output delta chunk
 * @return uint32_t delta chunk size
 */
uint32_t DeltaComp::DeltaEncode(uint8_t* base_chunk, uint32_t base_size,
    uint8_t* input_chunk, uint32_t input_size, uint8_t* delta_chunk) {
    //将输入数据块与基准数据块进行比较，生成增量编码（只存储差异部分）
    //uint8_t* base_chunk,    // 输入：基准块数据
    //uint32_t base_size,     // 输入：基准块大小
    //uint8_t* input_chunk,   // 输入：当前块数据（需要编码的块）
    //uint32_t input_size,    // 输入：当前块大小
    //uint8_t* delta_chunk    // 输出：增量编码数据（差异数据）


    //------------------空指针检查
    if (!base_chunk || base_size == 0 || !input_chunk || input_size == 0 || !delta_chunk) {
        tool::Logging(my_name_.c_str(), "delta encode input null, skip delta.\n");
        return 0;                       // 返回0表示“放弃delta压缩”
    }


    //xdelta3参数          我们的参数
    //==========================================================
    //input               → input_chunk    (当前块)
    //input_size          → input_size     (当前块大小)
    //source              → base_chunk     (基准块)
    //source_size         → base_size      (基准块大小)
    //output_buffer       → delta_chunk    (输出缓冲区)
    //output_size         → &ret_size      (输出实际大小)
    //output_max          → ENC_MAX_CHUNK_SIZE (最大输出大小)
    //flags               → delta_flag_    (编码标志)


    //---------调用xdelta3库进行编码
    uint64_t ret_size = 0;
    int ret = xd3_encode_memory(input_chunk, input_size, base_chunk, base_size,
        delta_chunk, &ret_size, ENC_MAX_CHUNK_SIZE, delta_flag_);
    if (ret != 0) {
        tool::Logging(my_name_.c_str(), "delta encoding fails: %d.\n",
            ret);
        //------------
        return 0;       // 编码失败，返回0           // 第2处：改成 return 0，而不是 exit!!
        //-----------
        //exit(EXIT_FAILURE);
    }
    return ret_size;
}

/**
 * @brief perform delta decoding
 * 
 * @param base_chunk base chunk data
 * @param base_size base chunk size
 * @param delta_chunk delta chunk data
 * @param delta_size delta chunk size
 * @param output_chunk output chunk data <output>
 * @return uint32_t output chunk size
 */
uint32_t DeltaComp::DeltaDecode(uint8_t* base_chunk, uint32_t base_size,
    uint8_t* delta_chunk, uint32_t delta_size, uint8_t* output_chunk) {
    uint64_t ret_size = 0;
    //------空指针检查
    if (!base_chunk || base_size == 0 || !delta_chunk || delta_size == 0 || !output_chunk) {
        tool::Logging(my_name_.c_str(), "delta decode input null, skip delta.\n");
        return 0;
    }
    //-------


    int ret = xd3_decode_memory(delta_chunk, delta_size, base_chunk, base_size,
        output_chunk, &ret_size, ENC_MAX_CHUNK_SIZE, delta_flag_);
    if (ret != 0) {
        tool::Logging(my_name_.c_str(), "delta decoding fails: %d.\n",
            ret);
        //---------
        return 0;                       // 第4处：也改成 return 0
        //--------
        //exit(EXIT_FAILURE);
    }
    return ret_size;
}