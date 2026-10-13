/**
 * @file chunker_factory.h
 * @author Zuoru YANG (zryang@cse.cuhk.edu.hk)
 * @brief the factory of chunker
 * @version 0.1
 * @date 2022-06-05
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#ifndef MY_CODEBASE_CHUNKER_FACTORY_H
#define MY_CODEBASE_CHUNKER_FACTORY_H

#include "abs_chunker.h"
#include "fix_chunker.h"
#include "fastcdc_chunker.h"

// the type of chunker
enum CHUNKER_TYPE {FIXED_SIZE_CHUNKING = 0,// 固定大小分块
    FAST_CDC,// FastCDC分块算法
    FSL_TRACE, // FSL跟踪文件分块
    MS_TRACE};// MS跟踪文件分块



class ChunkerFactory {
    private:
        string my_name_ = "ChunkerFactory";
    public:
        ChunkerFactory() {
            ;
        }

        ~ChunkerFactory() {
            ;
        }

        AbsChunker* CreateChunker(int type) {
            switch (type) {
                case FAST_CDC: {//config.json文件里设置的  "chunking_type": 1
                    tool::Logging(my_name_.c_str(), "using FastCDC chunking.\n");
                    return new FastCDC();// 创建FastCDC分块器
                }
                case FIXED_SIZE_CHUNKING: {
                    tool::Logging(my_name_.c_str(), "using Fixed-size chunking.\n");
                    return new FixChunker(); // 创建固定大小分块器
                }
                default: {
                    tool::Logging(my_name_.c_str(), "wrong chunker type.\n");
                    exit(EXIT_FAILURE);     // 无效类型处理
                }
            }
            return NULL;
        }
};

#endif