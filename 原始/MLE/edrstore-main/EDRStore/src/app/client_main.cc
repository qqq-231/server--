/**
 * @file client_main.cc
 * @author Zuoru YANG (zryang@cse.cuhk.edu.hk)
 * @brief the main program of client
 * @version 0.1
 * @date 2022-06-07
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include "../../include/configure.h"
//作用：系统配置管理
//功能：
//解析JSON配置文件（config.json）
//提供全局配置参数访问接口
//管理客户端ID、服务器IP/端口、加密算法类型等





// for upload 上传
#include "../../include/client/chunker_fp_thd.h" // 分块和指纹计算
#include "../../include/client/plain_similar_thd.h"// 明文相似性检测
#include "../../include/client/key_gen_thd.h" // 密钥生成
#include "../../include/client/cipher_similar_thd.h"// 加密相似性检测
#include "../../include/client/select_comp_thd.h" // 压缩选择
#include "../../include/client/sender_thd.h" // 数据发送

// for download 下载
#include "../../include/client/download_writer_thd.h"
#include "../../include/util/performance_stats.h"
#include "../../include/client/data_retriever_thd.h"

#include <boost/thread/thread.hpp>

using namespace std;

//struct timeval ss;
//struct timeval ee;

struct timeval stime;
struct timeval etime;
double total_time = 0;

uint64_t total_data_size = 0;
uint64_t total_chunk_num = 0;
string client_stat_file_name = "client-stat";

Configure config("config.json");// 加载配置文件
string my_name = "ClientMain";
string log_file_name = "client-log";
ofstream log_file;
MQFactory<Chunk_t> chunk_mq_factory;
MQFactory<FeatureChunk_t> feature_chunk_mq_factory;
MQFactory<EncFeatureChunk_t> enc_feature_chunk_mq_factory;
MQFactory<SelectComp2Sender_t> select_comp_2_sender_mq_factory;
MQFactory<Retriever2Writer_t> retriever_2_writer_mq_factory;

uint32_t MQ_TYPE = LCK_FREE_MQ;

#ifdef EDR_BREAKDOWN
string breakdown_file_name = "breakdown-client-file";
ofstream breakdown_client_file_hdl;
string breakdown_stat_file_name = "breakdown-stat";
#endif




//支持三种模式（-m参数）：
//0：仅相似性感知加密
//1：加密+本地压缩
//2：完整EDR（加密+压缩+重复数据删除）


void Usage() {
    fprintf(stderr, "%s -t [u/d] -i [input file path] -m [method] -c [client id] "
        "-o [output dir].\n"
        "-t: operation ([u/d]):\n"
        "\tu: upload\n"
        "\td: download\n"
        "-m: method type:\n"
        "\t0: similar-aware encryption\n"
        "\t1: similar-aware encryption + local compression\n"
        "\t2: full EDR\n"
        "-c: client id (optional, default from config.json)\n"
        "-o: output directory for download (optional, default: same as -i path)\n",
        my_name.c_str());
    return ;
}

#ifdef EDR_BREAKDOWN
void PrintClientPerformanceInfo(const string& dataset, uint32_t opt_type,
    uint64_t logical_size, uint64_t operation_time_ns,
    uint64_t encryption_time_ns, uint64_t key_generation_time_ns,
    uint64_t feature_extraction_time_ns, uint64_t similarity_search_time_ns) {
    const double operation_time_ms = PerformanceStats::NsToMs(
        operation_time_ns);
    const double throughput = operation_time_ns == 0 ? 0.0 :
        PerformanceStats::BytesToMB(logical_size) /
        (static_cast<double>(operation_time_ns) / 1000000000.0);
    const double logical_gb = static_cast<double>(logical_size) / 1000000000.0;
    const double encryption_time_ms = PerformanceStats::NsToMs(
        encryption_time_ns);
    const double key_generation_time_ms = PerformanceStats::NsToMs(
        key_generation_time_ns);

    breakdown_client_file_hdl << fixed << setprecision(3);
    breakdown_client_file_hdl << "======== Performance Info ========\n";
    breakdown_client_file_hdl << "Dataset: " << dataset << "\n";
    breakdown_client_file_hdl << "Version: ORIGINAL\n";
    breakdown_client_file_hdl << "Status: " << (opt_type == UPLOAD_OPT ?
        "UPLOAD_SUCCESS" : "DOWNLOAD_SUCCESS") << "\n";
    breakdown_client_file_hdl << "Logical Size(Byte): " << logical_size << "\n";
    breakdown_client_file_hdl << "Upload Time(ms): " <<
        (opt_type == UPLOAD_OPT ? operation_time_ms : 0.0) << "\n";
    breakdown_client_file_hdl << "Upload Throughput(MB/s): " <<
        (opt_type == UPLOAD_OPT ? throughput : 0.0) << "\n";
    breakdown_client_file_hdl << "Download Time(ms): " <<
        (opt_type == DOWNLOAD_OPT ? operation_time_ms : 0.0) << "\n";
    breakdown_client_file_hdl << "Download Throughput(MB/s): " <<
        (opt_type == DOWNLOAD_OPT ? throughput : 0.0) << "\n";
    breakdown_client_file_hdl << "Encryption Time(ms): " <<
        encryption_time_ms << "\n";
    breakdown_client_file_hdl << "Encryption Cost(ms/GB): " <<
        (logical_gb == 0.0 ? 0.0 : encryption_time_ms / logical_gb) << "\n";
    breakdown_client_file_hdl << "Key Generation Time(ms): " <<
        key_generation_time_ms << "\n";
    breakdown_client_file_hdl << "Key Generation Cost(ms/GB): " <<
        (logical_gb == 0.0 ? 0.0 : key_generation_time_ms / logical_gb) << "\n";
    breakdown_client_file_hdl << "Feature Extraction Time(ms): " <<
        PerformanceStats::NsToMs(feature_extraction_time_ns) << "\n";
    breakdown_client_file_hdl << "Similarity Search Time(ms): " <<
        PerformanceStats::NsToMs(similarity_search_time_ns) << "\n";
    breakdown_client_file_hdl << "Peak Memory(MB): " <<
        static_cast<double>(tool::GetMaxMemoryUsage()) / 1024.0 << "\n";
    breakdown_client_file_hdl << "==================================\n";
    breakdown_client_file_hdl.flush();
}
#endif

int main(int argc, char* argv[]) {
    // for log file
    if (!tool::FileExist(log_file_name)) {
        // if the log file not exist, add the header
        log_file.open(log_file_name, ios_base::out);
        log_file << "total data size, " << "total chunk num, "
            << "input file, " << "opt, "
            << "logical data size, " << "logical chunk num, "
            << "client similar chunk num, " << "total time, "
            << "speed (MiB/s), " << "upload data traffic, "
            << "version index size" << endl;
    } else {
        // the log file exists
        log_file.open(log_file_name, ios_base::app | ios_base::out);
        ifstream client_stat_hdl;
        client_stat_hdl.open(client_stat_file_name, ios_base::in | ios_base::binary);
        client_stat_hdl.read((char*)&total_chunk_num, sizeof(uint64_t));
        client_stat_hdl.read((char*)&total_data_size, sizeof(uint64_t));
        client_stat_hdl.close();
    }

#ifdef EDR_BREAKDOWN
    if (!tool::FileExist(breakdown_file_name)) {
        // if the breakdown file not exist, add the header
        breakdown_client_file_hdl.open(breakdown_file_name, ios_base::out);
        breakdown_client_file_hdl << "chunking size, " << "chunking time, "
            << "fp size, " << "fp time, "
            << "p_feature size, " << "p_feature time, "
            << "two_enc size, " << "two_enc time, " << "key gen time, "
            << "c_feature size, " << "c_feature time, "
            << "comp_pad size, " << "comp_pad time, "
            << "cache manage size, " << "cache manage time" << endl;
    } else {
        // the log file exists
        breakdown_client_file_hdl.open(breakdown_file_name, ios_base::app | ios_base::out);
    }
#endif

    // -------- main process --------

  //      gettimeofday(&ss, NULL);
    // printf("%d", ee.tv_sec);
    // printf("%d", ee.tv_usec);
    //cout<<ss.tv_sec<<" "<<ss.tv_usec<<endl;

    const char opt_str[] = "t:i:m:c:o:";
    int option;

    if (argc < (int)(sizeof(opt_str) - 1)) {
        tool::Logging(my_name.c_str(), "wrong argc: %d\n", argc);
        Usage();
        exit(EXIT_FAILURE);
    }

    uint32_t opt_type;
    string input_file_path;
    string output_dir;
    uint32_t method_type;
    int client_id_override = -1;
    while ((option = getopt(argc, argv, opt_str)) != -1) {
        switch (option) {
            case 't': {
                if (strcmp("u", optarg) == 0) {
                    opt_type = UPLOAD_OPT;
                } else if (strcmp("d", optarg) == 0) {
                    opt_type = DOWNLOAD_OPT;
                } else {
                    tool::Logging(my_name.c_str(), "wrong operation type.\n");
                    Usage();
                    exit(EXIT_FAILURE);
                }
                break;
            }
            case 'i': {
                input_file_path.assign(optarg);
                break;
            }
            case 'm': {
                switch (atoi(optarg)) {
                    case ONLY_SIMILAR_ENC: {
                        method_type = ONLY_SIMILAR_ENC;
                        break;
                    }
                    case SIMILAR_ENC_COMP: {
                        method_type = SIMILAR_ENC_COMP;
                        break;
                    }
                    case FULL_EDR: {
                        method_type = FULL_EDR;
                        break;
                    }
                    default: {
                        tool::Logging(my_name.c_str(), "wrong EDRStore design type.\n");
                        Usage();
                        exit(EXIT_FAILURE);
                    }
                }
                break;
            }
            case 'c': {
                client_id_override = atoi(optarg);
                break;
            }
            case 'o': {
                output_dir.assign(optarg);
                break;
            }
            case '?': {
                tool::Logging(my_name.c_str(), "error optopt: %c\n", optopt);
                tool::Logging(my_name.c_str(), "error opterr: %d\n", opterr);
                Usage();
                exit(EXIT_FAILURE);
            }
        }
    }

//用于存储指向所有工作线程的指针，方便后续统一管理（如等待所有线程结束）。
    vector<boost::thread*> thd_list;
    boost::thread* tmp_thd;//临时线程指针，用于创建新线程并加入到`thd_list`中。
    boost::thread::attributes thd_attrs;//线程属性，用于设置线程的栈大小等属性。

    // for upload operation
    ChunkerFPThd* chunk_fp_thd = nullptr;
    PlainSimilarThd* plain_similar_thd = nullptr;
    KeyGenThd* key_gen_thd = nullptr;
    CipherSimilarThd* cipher_similar_thd = nullptr;
    SelectCompThd* select_comp_thd = nullptr;
    SenderThd* sender_thd = nullptr;
    CacheMeta* cache_meta = nullptr;

    // for download operation
    DownloadWriterThd* download_writer_thd = nullptr;
    DataRetrieverThd* data_retriever_thd = nullptr;

    // for connection
    SSLConnection* server_channel;//用于与存储服务器建立SSL连接的连接对象
    pair<int, SSL*> server_conn_record;//存储连接描述符和SSL指针（由`ConnectSSL`返回）
    SSLConnection* km_channel = nullptr;//用于与密钥管理服务器（Key Management Server）建立连接。
    pair<int, SSL*> km_conn_record;

    //建立存储服务器安全连接
    server_channel = new SSLConnection(config.GetStorageServerIP(),
        config.GetStorageServerPort(), IN_CLIENT_SIDE);
    server_conn_record = server_channel->ConnectSSL();

    // compute the file name hash  // 计算文件名哈希
    if (client_id_override > 0) {
        config.SetClientID((uint32_t)client_id_override);
    }
    uint32_t client_id = config.GetClientID();
    string full_name = input_file_path + to_string(client_id);
    uint8_t file_name_hash[CHUNK_HASH_SIZE] = {0};
    // 使用加密工具生成哈希
    CryptoUtil* crypto_util = new CryptoUtil(CIPHER_TYPE, HASH_TYPE);
    EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
    crypto_util->GenerateHash(md_ctx, (uint8_t*)&full_name[0], full_name.size(),
        file_name_hash);// SHA-256哈希
    delete crypto_util;
    EVP_MD_CTX_free(md_ctx);

    const auto performance_operation_start = PerformanceStats::Clock::now();

//判断是执行上传还是下载操作
    switch (opt_type) {
        case UPLOAD_OPT: {
            ifstream input_file_hdl;
            tool::Logging(my_name.c_str(), "upload input file name: %s\n",
                input_file_path.c_str());
            input_file_hdl.open(input_file_path, ios_base::in | ios_base::binary);
            if (!input_file_hdl.is_open()) {
                tool::Logging(my_name.c_str(), "cannot open the input file: %s\n",
                    input_file_path.c_str());
                exit(EXIT_FAILURE);
            }

//线程对象初始化（上传流水线组件）
            chunk_fp_thd = new ChunkerFPThd();// 文件分块和指纹计算
            plain_similar_thd = new PlainSimilarThd(); // 明文相似性检测
            km_channel = new SSLConnection(config.GetKeyServerIP(),
                config.GetKeyServerPort(), IN_CLIENT_SIDE);// 连接密钥服务器
            km_conn_record = km_channel->ConnectSSL();
            key_gen_thd = new KeyGenThd(km_channel, km_conn_record);// 密钥生成
            cipher_similar_thd = new CipherSimilarThd();// 加密相似性检测
            cache_meta = new CacheMeta(server_channel, server_conn_record);// 缓存管理
            select_comp_thd = new SelectCompThd(cache_meta, method_type);// 压缩选择
            sender_thd = new SenderThd(server_channel, server_conn_record,
                file_name_hash, cache_meta); // 数据发送

#ifdef EDR_BREAKDOWN
            // restore the breakdown status  性能统计恢复（EDR_BREAKDOWN模式）

            //恢复11项关键性能指标：
            //分块数据量和时间
            //指纹计算数据量和时间
            //明文特征提取数据量和时间
            //双加密数据量和时间
            //密钥生成时间
            //加密特征数据量和时间
            //压缩/填充数据量和时间
            //缓存管理数据量和时间

            ifstream in_breakdown_stat_hdl;// 创建输入文件流对象
            //读取二进制数据
            // chunking + fp thread
            in_breakdown_stat_hdl.open(breakdown_stat_file_name, ios_base::in);
            in_breakdown_stat_hdl.read((char*)&chunk_fp_thd->_total_chunking_data_size,
                sizeof(uint64_t));
            in_breakdown_stat_hdl.read((char*)&chunk_fp_thd->_total_chunking_time,
                sizeof(double));
            in_breakdown_stat_hdl.read((char*)&chunk_fp_thd->_total_fp_data_size,
                sizeof(uint64_t));
            in_breakdown_stat_hdl.read((char*)&chunk_fp_thd->_total_fp_time,
                sizeof(double));

            // plain similar thread
            in_breakdown_stat_hdl.read((char*)&plain_similar_thd->_total_plain_feature_size,
                sizeof(uint64_t));
            in_breakdown_stat_hdl.read((char*)&plain_similar_thd->_total_plain_feature_time,
                sizeof(double));

            // key gen thread
            in_breakdown_stat_hdl.read((char*)&key_gen_thd->_total_two_enc_size,
                sizeof(uint64_t));
            in_breakdown_stat_hdl.read((char*)&key_gen_thd->_total_two_enc_time,
                sizeof(double));
            in_breakdown_stat_hdl.read((char*)&key_gen_thd->_total_key_gen_time,
                sizeof(double));

            // cipher feature thread
            in_breakdown_stat_hdl.read((char*)&cipher_similar_thd->_total_cipher_feature_size,
                sizeof(uint64_t));
            in_breakdown_stat_hdl.read((char*)&cipher_similar_thd->_total_cipher_feature_time,
                sizeof(double));

            // select comp thread
            in_breakdown_stat_hdl.read((char*)&select_comp_thd->_total_comp_pad_size,
                sizeof(uint64_t));
            in_breakdown_stat_hdl.read((char*)&select_comp_thd->_total_comp_pad_time,
                sizeof(double));
            in_breakdown_stat_hdl.read((char*)&select_comp_thd->_total_cache_manage_size,
                sizeof(uint64_t));
            in_breakdown_stat_hdl.read((char*)&select_comp_thd->_total_cache_manage_time,
                sizeof(double));

            in_breakdown_stat_hdl.close();//关闭之前打开的输入文件流  确保文件内容缓存完全写入磁盘
#endif

            // 创建5个消息队列（MQ = Message Queue）
            AbsMQ<Chunk_t>* chunker_mq = chunk_mq_factory.CreateMQ(MQ_TYPE,
                CHUNK_QUEUE_SIZE); // 分块->明文相似性检测
            AbsMQ<FeatureChunk_t>* plain_similar_mq =
                feature_chunk_mq_factory.CreateMQ(MQ_TYPE, CHUNK_QUEUE_SIZE);// 明文相似性->密钥生成
            AbsMQ<EncFeatureChunk_t>* key_gen_mq =
                enc_feature_chunk_mq_factory.CreateMQ(MQ_TYPE, CHUNK_QUEUE_SIZE); // 密钥生成->密文相似性
            AbsMQ<EncFeatureChunk_t>* cipher_similar_mq =
                enc_feature_chunk_mq_factory.CreateMQ(MQ_TYPE, CHUNK_QUEUE_SIZE);// 密文相似性->压缩选择
            AbsMQ<SelectComp2Sender_t>* select_comp_mq =
                select_comp_2_sender_mq_factory.CreateMQ(MQ_TYPE, CHUNK_QUEUE_SIZE); // 压缩选择->发送


            // send the upload login to notify the server   向服务器注册上传任务
            bool login_ok = sender_thd->UploadLogin(file_name_hash);
            if (!login_ok) {
                tool::Logging(my_name.c_str(), "upload login failed, aborting.\n");
            }

            if (login_ok) {

       //将各个线程放进thd_list里------后续直接按顺序启动
       //boost::thread创建了多个线程，每个线程都绑定了一个特定类的Run方法，并传递了消息队列（MQ）作为参数。
       //这些线程组成了一个处理流水线（pipeline），每个线程负责处理一个阶段，并通过消息队列将数据传递给下一个阶段

        //分块指纹线程
       //功能：负责读取输入文件，将其分块，并计算每个块的指纹（例如使用Rabin指纹分块算法）。然后将分块信息
       //（如块指纹、块数据等）放入`chunker_mq`消息队列，供下一个线程使用。
            tmp_thd = new boost::thread(thd_attrs, boost::bind(&ChunkerFPThd::Run,
                chunk_fp_thd, &input_file_hdl, chunker_mq));
                thd_list.push_back(tmp_thd);

          //明文相似性检测
          //功能：从`chunker_mq`中取出分块信息，检测这些明文块之间的相似性（例如，通过查找重复的块或者相似的块
          //），然后将处理后的数据（例如去重后的块或者相似块的信息）放入`plain_similar_mq`队列。
            tmp_thd = new boost::thread(thd_attrs, boost::bind(&PlainSimilarThd::Run,
                plain_similar_thd, chunker_mq, plain_similar_mq));
                thd_list.push_back(tmp_thd);

            //密钥生成线程
            //根据明文块的信息生成加密密钥（可能针对每个块或一组块生成密钥），然后将密钥信息以及关联的块信息放入`key_gen_mq`队列。
            tmp_thd = new boost::thread(thd_attrs, boost::bind(&KeyGenThd::Run,
                key_gen_thd, plain_similar_mq, key_gen_mq));
            thd_list.push_back(tmp_thd);

               //密文相似性检测线程
             //使用生成的密钥对块进行加密，并检测加密后的密文块之间的相似性（例如，即使明文相似，加密后是否仍然相似？）。然后将密文
             //块及其相似性信息放入`cipher_similar_mq`队列。
            tmp_thd = new boost::thread(thd_attrs, boost::bind(&CipherSimilarThd::Run, 
                cipher_similar_thd, key_gen_mq, cipher_similar_mq));
            thd_list.push_back(tmp_thd);


            //选择压缩线程
            //根据密文块的相似性信息，选择是否进行压缩（例如，对相似的密文块使用更高效的压缩方式），然后将处理后的数据（压缩后的数据
            //或原始数据）放入`select_comp_mq`队列。
            tmp_thd = new boost::thread(thd_attrs, boost::bind(&SelectCompThd::Run,
                select_comp_thd, cipher_similar_mq, select_comp_mq));
            thd_list.push_back(tmp_thd);

            //从`select_comp_mq`中取出数据，并将其发送到目的地（例如网络或存储系统）。
            tmp_thd = new boost::thread(thd_attrs, boost::bind(&SenderThd::Run,
                sender_thd, select_comp_mq));
            thd_list.push_back(tmp_thd);

      
      	    gettimeofday(&stime, NULL); // 开始计时
	    for (auto it : thd_list) {
                it->join();//启动thd_list里的线程执行
            }
            gettimeofday(&etime, NULL);//结束计时
		    total_time += tool::GetTimeDiff(stime, etime);
            const uint64_t performance_operation_time_ns =
                PerformanceStats::ElapsedNs(performance_operation_start);
            tool::Logging(my_name.c_str(), "%s finish.\n", input_file_path.c_str());

            for (auto it : thd_list) {
                delete it;//清空thd_list
            }

// 使用分块线程（chunk_fp_thd）中统计的文件总大小和总时间计算上传速度（MiB/s）
// 更新全局的总块数（total_chunk_num）和总数据量（total_data_size）
            double speed = static_cast<double>(chunk_fp_thd->_total_file_size) /
                1024.0 / 1024.0 / total_time;
            total_chunk_num += chunk_fp_thd->_total_chunk_num;
            total_data_size += chunk_fp_thd->_total_file_size;
            //写入日志文件（log_file）
            log_file << total_data_size << ", "
                << total_chunk_num << ", " 
                << input_file_path << ", upload, "
                << chunk_fp_thd->_total_file_size << ", "
                << chunk_fp_thd->_total_chunk_num << ", "
                << cache_meta->_total_similar_chunk << ", "
                << total_time << ", " << speed << ", "
                << sender_thd->_total_send_data_size << ", "
                << sender_thd->_cur_version_idx_size << endl;

#ifdef EDR_BREAKDOWN
            PrintClientPerformanceInfo(input_file_path, UPLOAD_OPT,
                chunk_fp_thd->_total_file_size,
                performance_operation_time_ns,
                key_gen_thd->_performance_encryption_time_ns +
                    sender_thd->_performance_encryption_time_ns,
                key_gen_thd->_performance_key_generation_time_ns,
                plain_similar_thd->_performance_feature_time_ns +
                    cipher_similar_thd->_performance_feature_time_ns,
                select_comp_thd->_performance_similarity_search_time_ns);
#endif


            // 如果启用了EDR_BREAKDOWN，则写入性能分析文件：
            //  - 将各个阶段的性能数据写入breakdown_client_file_hdl（CSV格式）
            // - 同时将性能数据以二进制形式保存到breakdown_stat_file_name文件中，以便后续恢复（用于累计统计）

#ifdef EDR_BREAKDOWN
            breakdown_client_file_hdl << chunk_fp_thd->_total_chunking_data_size << ", "
                << chunk_fp_thd->_total_chunking_time << ", "
                << chunk_fp_thd->_total_fp_data_size << ", "
                << chunk_fp_thd->_total_fp_time << ", "
                << plain_similar_thd->_total_plain_feature_size << ", "
                << plain_similar_thd->_total_plain_feature_time << ", "
                << key_gen_thd->_total_two_enc_size << ", "
                << key_gen_thd->_total_two_enc_time << ", "
                << key_gen_thd->_total_key_gen_time << ", "
                << cipher_similar_thd->_total_cipher_feature_size << ", "
                << cipher_similar_thd->_total_cipher_feature_time << ", "
                << select_comp_thd->_total_comp_pad_size << ", "
                << select_comp_thd->_total_comp_pad_time << ", "
                << select_comp_thd->_total_cache_manage_size << ", "
                << select_comp_thd->_total_cache_manage_time << endl;
            
            // store the breakdown stat  // 保存详细性能数据
            ofstream out_breakdown_stat_hdl;//创建输出文件流对象
            out_breakdown_stat_hdl.open(breakdown_stat_file_name,
                ios_base::trunc | ios_base::binary);
            // chunking + fp thread   分块+指纹
            out_breakdown_stat_hdl.write((char*)&chunk_fp_thd->_total_chunking_data_size,// 分块处理的总数据量
                sizeof(uint64_t));
            out_breakdown_stat_hdl.write((char*)&chunk_fp_thd->_total_chunking_time,//分块处理的总时间
                sizeof(double));//要求双精度写入
            out_breakdown_stat_hdl.write((char*)&chunk_fp_thd->_total_fp_data_size,//计算指纹的数据量（通常等于分块数据量）
                sizeof(uint64_t));
            out_breakdown_stat_hdl.write((char*)&chunk_fp_thd->_total_fp_time,//计算指纹的总时间
                sizeof(double));
            
            // plain similar thread   明文相似性检测线程
            out_breakdown_stat_hdl.write((char*)&plain_similar_thd->_total_plain_feature_size,//明文特征提取的数据量
                sizeof(uint64_t));
            out_breakdown_stat_hdl.write((char*)&plain_similar_thd->_total_plain_feature_time,//明文特征提取的总时间
                sizeof(double));
            
            // key gen thread        密钥生成
            out_breakdown_stat_hdl.write((char*)&key_gen_thd->_total_two_enc_size,//双加密操作的数据量
                sizeof(uint64_t));
            out_breakdown_stat_hdl.write((char*)&key_gen_thd->_total_two_enc_time,//双加密操作的时间
                sizeof(double));
            out_breakdown_stat_hdl.write((char*)&key_gen_thd->_total_key_gen_time,//密钥生成的时间（包括与密钥服务器的通信）
                sizeof(double));

            // cipher similar thread    加密相似性检测线程
            out_breakdown_stat_hdl.write((char*)&cipher_similar_thd->_total_cipher_feature_size,//加密特征处理的数据量
                sizeof(uint64_t));
            out_breakdown_stat_hdl.write((char*)&cipher_similar_thd->_total_cipher_feature_time,//加密特征处理的时间
                sizeof(double));

            // select comp thread   压缩选择线程
            out_breakdown_stat_hdl.write((char*)&select_comp_thd->_total_comp_pad_size,//压缩和填充操作的数据量
                sizeof(uint64_t));
            out_breakdown_stat_hdl.write((char*)&select_comp_thd->_total_comp_pad_time,//压缩和填充的时间
                sizeof(double));
            out_breakdown_stat_hdl.write((char*)&select_comp_thd->_total_cache_manage_size,//缓存管理操作涉及的数据量
                sizeof(uint64_t));
            out_breakdown_stat_hdl.write((char*)&select_comp_thd->_total_cache_manage_time,//缓存管理的时间
                sizeof(double));

            out_breakdown_stat_hdl.close();//写文件显式关闭
#endif

} // end if (login_ok)

            //关闭输入文件句柄
            input_file_hdl.close();
            //删除所有线程对象
            delete chunk_fp_thd;
            delete plain_similar_thd;
            delete key_gen_thd;
            delete cipher_similar_thd;
            delete select_comp_thd;
            delete sender_thd;
            delete km_channel;
            delete cache_meta;

            // clear the MQ   删除消息队列
            delete chunker_mq;
            delete plain_similar_mq;
            delete cipher_similar_mq;
            delete key_gen_mq; 
            delete select_comp_mq;

            //更新客户端状态文件
            //将累计的总块数（total_chunk_num）和总数据量（total_data_size）以二进制形式写入client_stat_file_name文件
            ofstream client_stat_hdl;
            client_stat_hdl.open(client_stat_file_name, ios_base::trunc | ios_base::binary);
            client_stat_hdl.write((char*)&total_chunk_num, sizeof(uint64_t));
            client_stat_hdl.write((char*)&total_data_size, sizeof(uint64_t));
            client_stat_hdl.close();

            break;
        }
        case DOWNLOAD_OPT: {
            tool::Logging(my_name.c_str(), "download input file name: %s\n",
                input_file_path.c_str());

            // 计算实际下载输出路径
            string download_path;
            if (!output_dir.empty()) {
                // 从 -i 路径提取文件名，拼到 -o 目录下
                string base_name = input_file_path.substr(
                    input_file_path.find_last_of("/\\") + 1);
                download_path = output_dir + "/" + base_name;
            } else {
                download_path = input_file_path;
            }

            // 1. 创建 DownloadWriterThd（ID隔离：输出文件带 client_id 后缀）
            download_writer_thd = new DownloadWriterThd(download_path, client_id);

            // 2. 获取断点续传起始位置（0 = 全新下载）
            uint64_t start_chunk_idx = download_writer_thd->GetStartChunkIdx();

            // 3. 创建 DataRetrieverThd
            data_retriever_thd = new DataRetrieverThd(server_channel,
                server_conn_record, file_name_hash, method_type);

            // 消息队列创建
            AbsMQ<Retriever2Writer_t>* retriever_mq =
                retriever_2_writer_mq_factory.CreateMQ(MQ_TYPE, CHUNK_HASH_SIZE);

            // 4. 下载登录认证（传入 start_chunk_idx 支持断点续传）
            data_retriever_thd->DownloadLogin(file_name_hash, start_chunk_idx);

            // 5. 传递 recipe_head 给 writer（用于进度文件初始化）
            download_writer_thd->SetFileRecipeHead(
                data_retriever_thd->GetRecipeHead(), file_name_hash);

            // 启动下载流水线
            tmp_thd = new boost::thread(thd_attrs, boost::bind(&DataRetrieverThd::Run,
                data_retriever_thd, retriever_mq));
            thd_list.push_back(tmp_thd);
            tmp_thd = new boost::thread(thd_attrs, boost::bind(&DownloadWriterThd::Run,
                download_writer_thd, retriever_mq));
            thd_list.push_back(tmp_thd);

            // 时间统计与同步
            gettimeofday(&stime, NULL);
            for (auto it : thd_list) {
                it->join();
            }
            gettimeofday(&etime, NULL);
            total_time += tool::GetTimeDiff(stime, etime);
            const uint64_t performance_operation_time_ns =
                PerformanceStats::ElapsedNs(performance_operation_start);
            tool::Logging(my_name.c_str(), "%s finish.\n", input_file_path.c_str());

            for (auto it : thd_list) {
                delete it;
            }

            // 性能日志记录
            double speed = static_cast<double>(data_retriever_thd->_total_recv_data_size) /
                1024.0 / 1024.0 / total_time;
            log_file << total_data_size << ", "
                << total_chunk_num << ", "
                << input_file_path << ", download, "
                << data_retriever_thd->_total_recv_data_size << ", "
                << data_retriever_thd->_total_recv_chunk_num << ", "
                << "0, " << total_time << ", " << speed << endl;

#ifdef EDR_BREAKDOWN
            PrintClientPerformanceInfo(input_file_path, DOWNLOAD_OPT,
                data_retriever_thd->_total_recv_data_size,
                performance_operation_time_ns, 0, 0, 0, 0);
#endif

            delete data_retriever_thd;
            delete download_writer_thd;

            delete retriever_mq;
            break;
        }
    }

    // clear the connection  全局资源清理
    delete server_channel;//关闭服务器连接

#ifdef EDR_BREAKDOWN
    breakdown_client_file_hdl.close();// 关闭性能日志文件
#endif

    tool::Logging(my_name.c_str(), "total running time: %lf\n", total_time);    
    log_file.close();//日志关闭
    tool::Logging(my_name.c_str(), "max mem usage: %lu KiB\n",
        tool::GetMaxMemoryUsage());
    
    // cout<< std::put_time(std::localtime(&t), "%F %T ")
    // std::cout << system_clock::now() << '\n';
//    gettimeofday(&ee, NULL);
    // printf("%d", ee.tv_sec);
    // printf("%d", ee.tv_usec);
  //  cout<<ee.tv_sec<<" "<<ee.tv_usec<<endl;
    

    return 0;
}
