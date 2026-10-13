/**
 * @file fastcdc_chunker.cc
 * @author Zuoru YANG (zryang@cse.cuhk.edu.hk)
 * @brief implement the interface of FastCDC
 * @version 0.1
 * @date 2022-06-04
 * 
 * @copyright Copyright (c) 2022
 * 
 */

//--------------------------------------------------
//实现了 FastCDC 分块算法的具体逻辑，是 fastcdc_chunker.h 头文件中声明的类方法的实现
//-------------------------------------------------

#include "../../include/chunker/fastcdc_chunker.h"

/**
 * @brief Construct a new FastCDC object
 * 
 */
FastCDC::FastCDC() {//初始化关键参数    根据平均分块大小动态生成检测掩码
// 计算标准分块大小
    normal_chunk_size_ = this->CalNormalSize(min_chunk_size_, avg_chunk_size_, max_chunk_size_);
       // 计算掩码位数：log2(平均分块大小)
    uint32_t bits = (uint32_t) round(log2(static_cast<double>(avg_chunk_size_)));
     // 生成大小掩码
    mask_s_ = GenerateFastCDCMask(bits + 1);// 小分块掩码
    mask_l_ = GenerateFastCDCMask(bits - 1);// 大分块掩码
    tool::Logging(my_name_.c_str(), "init FastCDC.\n");
}

/**
 * @brief Destroy the FastCDC object
 * 
 */
FastCDC::~FastCDC() {

}

/**
 * @brief load the data from the file
 * 
 * @param input_file the input file handler
 * @return uint32_t the read size
 */
uint32_t FastCDC::LoadDataFromFile(ifstream& input_file) {//从文件读取数据
// 读取数据到缓冲区
    input_file.read((char*)read_data_buf_, read_size_);
    // 获取实际读取大小
    pending_chunking_size_ = input_file.gcount();

    // reset the offset    // 重置处理状态
    cur_offset_ = 0;
    remain_chunking_size_ = pending_chunking_size_;

    return pending_chunking_size_;
}

/**
 * @brief generate a chunk
 * 
 * @param data the buffer to store the chunk data
 * @return uint32_t the chunk size
 */
uint32_t FastCDC::GenerateOneChunk(uint8_t* data) {// 分块生成
    if (pending_chunking_size_ == 0 || remain_chunking_size_ == 0) {
        return 0; // this is the end of the pending buffer// 缓冲区已空
    }
    
    uint32_t chunk_size = 0;
     // 处理小数据块（小于最小分块大小）
    if (remain_chunking_size_ <= min_chunk_size_) {
        memcpy(data, read_data_buf_ + cur_offset_, remain_chunking_size_);
        chunk_size = remain_chunking_size_;
    } else {  // 正常数据块处理
      // 计算分块边界点
        chunk_size = this->CutPoint(read_data_buf_ + cur_offset_, pending_chunking_size_ - cur_offset_);
        memcpy(data, read_data_buf_ + cur_offset_, chunk_size);
    }

  // 更新处理状态
    cur_offset_ += chunk_size;
    remain_chunking_size_ -= chunk_size;

 // 更新统计信息
    _total_chunk_num++;
    _total_file_size += chunk_size;
    return chunk_size;
}

/**
 * @brief compute the normal size 
 * 
 * @param min the min chunk size
 * @param avg the avg chunk size
 * @param max the max chunk size
 * @return uint32_t 
 */
uint32_t FastCDC::CalNormalSize(const uint32_t min, const uint32_t avg,// 标准分块大小计算
    const uint32_t max) {
    //公式：normal_size = min(avg - (min + min/2), max)
    uint32_t off = min + tool::DivCeil(min, 2); // 计算偏移量
    if (off > avg) {
        off = avg;
    } 
    uint32_t diff = avg - off;
    if (diff > max) {
        return max;
    }// 确保不超过最大值
    return diff;
}

/**
 * @brief generate the mask according to the given bits
 * 
 * @param bits the number of '1' + 1
 * @return uint32_t the returned mask
 */
uint32_t FastCDC::GenerateFastCDCMask(uint32_t bits) {//掩码生成
    uint32_t tmp;
     // 确保bits在1-31范围内   bits = tool::CompareLimit(bits, 1, 31);
    // 生成指定位数的掩码 (如bits=13 → 0x1FFF)     return (1 << bits) - 1
    //结合生成下面代码
    tmp = (1 << tool::CompareLimit(bits, 1, 31)) - 1;
    return tmp;
}

/**
 * @brief To get the offset of chunks for a given buffer  
 * 
 * @param src the input buffer  
 * @param len the length of this buffer
 * @return uint32_t length of this chunk.
 */
uint32_t FastCDC::CutPoint(const uint8_t* src, const uint32_t len) {//CutPoint（分块边界检测）******
    uint32_t n;
    uint32_t fp = 0;// 指纹值
    uint32_t i;
      // 第一阶段：处理最小分块范围
    i = std::min(len, static_cast<uint32_t>(min_chunk_size_)); 
    n = std::min(normal_chunk_size_, len);
      // 遍历最小到标准大小范围
    for (; i < n; i++) {
     // 滚动哈希计算：fp = (fp >> 1) + GEAR[当前字节]
        fp = (fp >> 1) + GEAR[src[i]];
           // 使用小掩码检测边界
        if (!(fp & mask_s_)) {
            return (i + 1); // 找到边界
        }
    }
  // 第二阶段：处理最大分块范围
    n = std::min(static_cast<uint32_t>(max_chunk_size_), len);
    for (; i < n; i++) {
    // 使用大掩码检测边界
        fp = (fp >> 1) + GEAR[src[i]];
        if (!(fp & mask_l_)) {
            return (i + 1);// 找到边界
        }
    }
      // 未找到边界，返回当前最大位置
    return i;
}