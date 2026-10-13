/**
 * @file abs_chunker.h
 * @author Zuoru YANG (zryang@cse.cuhk.edu.hk)
 * @brief the interface of abs chunker
 * @version 0.1
 * @date 2022-06-03
 * 
 * @copyright Copyright (c) 2022
 * 
 */

//----------------------------------
//定义了一个 抽象分块器基类（Abstract Chunker Base Class），它为不同类型的文件分块算法提供了统一的接口和基础实现
//--------------------------------------



#ifndef MY_CODEBASE_ABS_CHUNKER_H
#define MY_CODEBASE_ABS_CHUNKER_H

#include "../define.h"
#include "../configure.h"

using namespace std;

extern Configure config;

class AbsChunker {
    protected:
        string my_name_ = "AbsChunker";
        // chunk size setting   分块大小设置
        uint64_t min_chunk_size_ = 0;// 最小分块大小
        uint64_t avg_chunk_size_ = 0;// 平均分块大小
        uint64_t max_chunk_size_ = 0;// 最大分块大小
        
        // buffer    数据缓冲区管理
        uint64_t read_size_ = 0; // 当前从文件读取的数据量
        uint8_t* read_data_buf_; // 文件数据缓冲区指针

        // size      分块状态跟踪
        uint64_t pending_chunking_size_ = 0;// 待处理数据总量
        uint64_t remain_chunking_size_ = 0;// 当前缓冲区剩余未处理数据量
        uint64_t cur_offset_ = 0; // 当前处理位置在缓冲区中的偏移

        
    public:
        uint64_t _total_chunk_num = 0;// 总分块数量统计
        uint64_t _total_file_size = 0;// 总处理文件大小统计

        /**
         * @brief Construct a new Abs Chunker object
         * 
         */
        AbsChunker();

        /**
         * @brief Destroy the Abs Chunker object
         * 
         */
        virtual ~AbsChunker();

        /**
         * @brief load the data from the file
         * 
         * @param input_file the input file handler
         * @return uint32_t the read size
         */
        virtual uint32_t LoadDataFromFile(ifstream& input_file) = 0;// 从文件加载数据到缓冲区
        
        /**
         * @brief generate a chunk
         * 
         * @param data the buffer to store the chunk data
         * @return uint32_t the chunk size
         */
        virtual uint32_t GenerateOneChunk(uint8_t* data) = 0;// 从缓冲区生成一个数据块
};

#endif