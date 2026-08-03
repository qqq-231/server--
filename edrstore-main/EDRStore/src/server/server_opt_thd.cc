/**
 * @file server_opt_thd.cc
 * @author Zuoru YANG (zryang@cse.cuhk.edu.hk)
 * @brief implement the interfaces fo ServerOptThd 
 * @version 0.1
 * @date 2022-07-19
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include "../../include/server/server_opt_thd.h"

/**
 * @brief Construct a new ServerOptThd object
 * 
 * @param server_channel the server channel
 * @param fp_2_addr_db the fp to address index
 * @param feature_2_fp_db the feature to base hash index
 */
ServerOptThd::ServerOptThd(SSLConnection* server_channel, 
    AbsDatabase* fp_2_addr_db, AbsDatabase* feature_2_fp_db,
    AbsDatabase* compressed_feature_2_fp_db) {
    server_channel_ = server_channel;
    fp_2_addr_db_ = fp_2_addr_db;
    feature_2_fp_db_ = feature_2_fp_db;
    compressed_feature_2_fp_db_ = compressed_feature_2_fp_db;

    // init the upload
    storage_core_ = new StorageCore();
    
    data_recv_thd_ = new DataRecvThd(server_channel_, fp_2_addr_db_);
    cache_comp_thd_ = new CacheCompThd();
    data_writer_thd_ = new DataWriterThd(fp_2_addr_db_,
        compressed_feature_2_fp_db_, storage_core_, cache_comp_thd_);
    dual_dedup_thd_ = new DualDedupThd(fp_2_addr_db_);
    recompress_handler_thd_ = new RecompressHandlerThd(server_channel_);

    data_reader_thd_ = new DataReaderThd(fp_2_addr_db_, storage_core_);
    data_decode_thd_ = new DataDecoderThd(server_channel_);
    
    this->LoadStat();
}

/**
 * @brief Destroy the ServerOptThd object
 * 
 */
ServerOptThd::~ServerOptThd() {
    this->StoreStat();
    fprintf(stderr, "========ServerOptThd Info========\n");
    fprintf(stderr, "total recv upload req: %lu\n", 
        _total_upload_opt_num);
    fprintf(stderr, "total recv download req: %lu\n", 
        _total_download_opt_num);
    fprintf(stderr, "total recv upload req: %lu\n", 
        _total_upload_opt_num);
    fprintf(stderr, "total recv download req: %lu\n", 
        _total_download_opt_num);

          fprintf(stderr, "本地不相似度 %lf%%\n",
       (double)cache_comp_thd_->_total_delta_size/ cache_comp_thd_->_total_similar_data_size*100);
        fprintf(stderr, "全局不相似度 %lf%%\n",
       (double)data_writer_thd_->_total_delta_size / data_writer_thd_->_total_similar_data_size*100);
    fprintf(stderr, "--------相似度区间分布--------\n");
    fprintf(stderr, "区间\t\t本地相似块数\t全局相似块数\t合计\n");
    const char* intervals[] = {
        "[0.0~0.4)", "[0.4~0.5)", "[0.5~0.6)", "[0.6~0.7)",
        "[0.7~0.8)", "[0.8~0.9)", "[0.9~1.0]"
    };
    uint64_t local_hist_sum = 0;
    uint64_t global_hist_sum = 0;
    for (int i = 0; i < 7; i++) {
        uint64_t row_sum = cache_comp_thd_->_similarity_hist[i]
            + data_writer_thd_->_similarity_hist[i];
        fprintf(stderr, "%s\t%lu\t\t%lu\t\t%lu\n",
            intervals[i],
            cache_comp_thd_->_similarity_hist[i],
            data_writer_thd_->_similarity_hist[i],
            row_sum);
        local_hist_sum += cache_comp_thd_->_similarity_hist[i];
        global_hist_sum += data_writer_thd_->_similarity_hist[i];
    }
    fprintf(stderr, "---------------------------------\n");
    fprintf(stderr, "直方图合计\t%lu\t\t%lu\t\t%lu\n",
        local_hist_sum, global_hist_sum,
        local_hist_sum + global_hist_sum);
    fprintf(stderr, "原有计数器\t%lu\t\t%lu\t\t%lu\n",
        cache_comp_thd_->_total_similar_chunk_num,
        data_writer_thd_->_total_similar_chunk_num,
        cache_comp_thd_->_total_similar_chunk_num + data_writer_thd_->_total_similar_chunk_num);
    fprintf(stderr, "=================================\n");

    // Reporting must happen before destroying the worker objects. Reading
    // their counters after delete made experiment summaries undefined.
    delete data_recv_thd_;
    delete data_writer_thd_;
    delete cache_comp_thd_;
    delete dual_dedup_thd_;
    delete recompress_handler_thd_;
    delete data_reader_thd_;
    delete data_decode_thd_;
    delete storage_core_;

    for (auto it : client_lck_idx_) {
        delete it.second;
    }
}

/**
 * @brief the main thread
 * 
 * @param client_ssl 
 */
void ServerOptThd::Run(SSL* client_ssl) {
    boost::thread* tmp_thd;
    boost::thread_attributes attrs;
    attrs.set_stack_size(THREAD_STACK_SIZE);
    vector<boost::thread*> thd_list;

    SendMsgBuffer_t recv_buf;
    recv_buf.send_buf = (uint8_t*) malloc(sizeof(NetworkHead_t) + CHUNK_HASH_SIZE + sizeof(uint64_t));
    recv_buf.header = (NetworkHead_t*) recv_buf.send_buf;
    recv_buf.header->size = 0;
    recv_buf.data_buf = recv_buf.send_buf + sizeof(NetworkHead_t);
    uint32_t recv_size = 0;

    tool::Logging(my_name_.c_str(), "the main thread is running.\n");

    // start to receive the login data type
    if (!server_channel_->ReceiveData(client_ssl, recv_buf.send_buf,
        recv_size)) {
        tool::Logging(my_name_.c_str(), "recv the login message error, "
            "cleaning up connection.\n");
        server_channel_->ClearAcceptedClientSd(client_ssl);
        free(recv_buf.send_buf);
        return ;
    }

    // add a client lck here (ensure only one client with the same id) 在此添加客户端锁（确保同一ID只有一个客户端）
    uint32_t client_id = recv_buf.header->client_id;
    this->LockClientID(client_id);
    
    // -------- main process --------
    int opt_type = 0;
    switch (recv_buf.header->msg_type) {
        case CLIENT_LOGIN_UPLOAD: {
            opt_type = UPLOAD_OPT;
            break;
        }
        case CLIENT_LOGIN_DOWNLOAD: {
            opt_type = DOWNLOAD_OPT;
            break;
        }
        default: {
            tool::Logging(my_name_.c_str(), "wrong client login type.\n");
            free(recv_buf.send_buf);
            this->UnlockClientID(client_id);
            server_channel_->ClearAcceptedClientSd(client_ssl);
            return ;
        }
    }

    // check the file status        检查文件状态
    // convert the file name hash to the file path       // 将文件名哈希转换为文件路径
    char file_hash_buf[CHUNK_HASH_SIZE * 2 + 1];
    for (size_t i = 0; i < CHUNK_HASH_SIZE; i++) {
        sprintf(file_hash_buf + i * 2, "%02x", recv_buf.data_buf[i]);
    }
    string file_name;
    file_name.assign(file_hash_buf, CHUNK_HASH_SIZE * 2);
    string recipe_path = config.GetRecipeRootPath() + file_name +
        config.GetRecipeSuffix();
    if (!this->CheckFileStat(recipe_path, opt_type)) {
        recv_buf.header->msg_type = SERVER_FILE_NON_EXIST;
        if (!server_channel_->SendData(client_ssl, recv_buf.send_buf,
            sizeof(NetworkHead_t))) {
            tool::Logging(my_name_.c_str(), "send the file reply error.\n");
            server_channel_->ClearAcceptedClientSd(client_ssl);
            free(recv_buf.send_buf);
            this->UnlockClientID(client_id);
            return ;
        }

        // wait the client to close the connection  等待客户端关闭连接
        if (!server_channel_->ReceiveData(client_ssl, recv_buf.send_buf,
            recv_size)) {
            tool::Logging(my_name_.c_str(), "client close the socket connection.\n");
            server_channel_->ClearAcceptedClientSd(client_ssl);
        } else {
            tool::Logging(my_name_.c_str(), "client does not close the connection.\n");
            server_channel_->ClearAcceptedClientSd(client_ssl);
        }

        // clear the tmp variable       清除 tmp 变量
        free(recv_buf.send_buf);
        this->UnlockClientID(client_id);
        return ;
    } else {
        tool::Logging(my_name_.c_str(), "file status check successfully.\n");
    }
    // check done

    // init the vars for this client    初始化此客户端的变量
    ClientVar* cur_client;
    switch (opt_type) {
        case UPLOAD_OPT: {
            // update the req number
            _total_upload_opt_num++;
            tool::Logging(my_name_.c_str(), "recv the upload req from client: %u\n",
                client_id);
            cur_client = new ClientVar(client_id, client_ssl, UPLOAD_OPT,
                recipe_path);
            
            // receive data & data fp generation
            tmp_thd = new boost::thread(attrs, boost::bind(&DataRecvThd::Run,
                data_recv_thd_, cur_client));
            thd_list.push_back(tmp_thd);
            // TODO: add one thread for dual fp generation & dedup
            tmp_thd = new boost::thread(attrs, boost::bind(&DualDedupThd::Run,
                dual_dedup_thd_, cur_client));
            thd_list.push_back(tmp_thd);
            tmp_thd = new boost::thread(attrs, boost::bind(&CacheCompThd::Run,
                cache_comp_thd_, cur_client));
            thd_list.push_back(tmp_thd);
            tmp_thd = new boost::thread(attrs, boost::bind(
                &RecompressHandlerThd::Run,
                recompress_handler_thd_, cur_client));
            thd_list.push_back(tmp_thd);
            tmp_thd = new boost::thread(attrs, boost::bind(&DataWriterThd::Run,
                data_writer_thd_, cur_client));
            thd_list.push_back(tmp_thd);

            // send the upload-response to the client
            recv_buf.header->msg_type = SERVER_LOGIN_RESPONSE;
            if (!server_channel_->SendData(client_ssl, recv_buf.send_buf,
                sizeof(NetworkHead_t))) {
                tool::Logging(my_name_.c_str(), "send the upload-login response error.\n");
                server_channel_->ClearAcceptedClientSd(client_ssl);
                delete cur_client;
                free(recv_buf.send_buf);
                this->UnlockClientID(client_id);
                return ;
            }
            break;
        }
        case DOWNLOAD_OPT: {
            _total_download_opt_num++;
            tool::Logging(my_name_.c_str(), "recv the download req from client: %u\n",
                client_id);

            // 解析断点续传参数（扩展协议：body = file_name_hash + start_chunk_idx）
            uint64_t start_chunk_idx = 0;
            if (recv_buf.header->size >= CHUNK_HASH_SIZE + sizeof(uint64_t)) {
                memcpy(&start_chunk_idx, recv_buf.data_buf + CHUNK_HASH_SIZE,
                    sizeof(uint64_t));
            }

            cur_client = new ClientVar(client_id, client_ssl, DOWNLOAD_OPT,
                recipe_path, start_chunk_idx);

            // send the download-response to the client (include the file recipe header)
            recv_buf.header->msg_type = SERVER_LOGIN_RESPONSE;
            cur_client->_recipe_read_hdl.read((char*)recv_buf.data_buf,
                sizeof(FileRecipeHead_t));

            // 断点续传：跳过前 start_chunk_idx 个chunk指纹
            if (start_chunk_idx > 0) {
                cur_client->_recipe_read_hdl.seekg(start_chunk_idx * CHUNK_HASH_SIZE,
                    ios_base::cur);
                tool::Logging(my_name_.c_str(), "skip %lu chunks for resume download.\n",
                    start_chunk_idx);
            }
            if (!server_channel_->SendData(client_ssl, recv_buf.send_buf,
                sizeof(NetworkHead_t) + sizeof(FileRecipeHead_t))) {
                tool::Logging(my_name_.c_str(), "send the download-login response error.\n");
                server_channel_->ClearAcceptedClientSd(client_ssl);
                delete cur_client;
                free(recv_buf.send_buf);
                this->UnlockClientID(client_id);
                return ;
            }

            tmp_thd = new boost::thread(attrs, boost::bind(&DataReaderThd::Run,
                data_reader_thd_, cur_client));
            thd_list.push_back(tmp_thd);
            tmp_thd = new boost::thread(attrs, boost::bind(&DataDecoderThd::Run,
                data_decode_thd_, cur_client));
            thd_list.push_back(tmp_thd);

            break;
        }
    }

    double total_time = 0;
    struct timeval stime;
    struct timeval etime;
    gettimeofday(&stime, NULL);
    for (auto it : thd_list) {
        it->join();
    }
    gettimeofday(&etime, NULL);
    total_time += tool::GetTimeDiff(stime, etime);

    // clean up
    for (auto it : thd_list) {
        delete it;
    }
    thd_list.clear();

    // clean up client variables
    delete cur_client;
    if (opt_type == UPLOAD_OPT) {
        InformCache cache_for_log;
        // Reclaim bases whose last exact client/feature reference expired.
        // This also performs the one-time cleanup of legacy aggregate-only
        // metadata after upgrading to exact association tracking.
        uint64_t total_cache_size = cache_for_log.DeleteEvictChunk();
        this->PrintClientLog(total_cache_size);
    }
    free(recv_buf.send_buf);
    this->UnlockClientID(client_id);

    tool::Logging(my_name_.c_str(), "total running time of client %u: %lf\n",
        client_id, total_time); 

    return ;
}

/**
 * @brief lock the mutex of a given client id
 * 
 * @param client_id the client id
 */
void ServerOptThd::LockClientID(uint32_t client_id) {
    boost::mutex* tmp_lck;
    {
        lock_guard<mutex> lck(client_idx_lck_);
        auto client_lck_ret = client_lck_idx_.find(client_id);
        if (client_lck_ret != client_lck_idx_.end()) {
            // try to acquire the lck
            tmp_lck = client_lck_idx_[client_id];
        } else {
            // add a new lck to the current index
            tmp_lck = new boost::mutex();
            client_lck_idx_[client_id] = tmp_lck;
        }
    }
    tmp_lck->lock();
    return ;
}

/**
 * @brief unlock the mutex of a given client id
 * 
 * @param client_id the client id
 */
void ServerOptThd::UnlockClientID(uint32_t client_id) {
    boost::mutex* tmp_lck;
    {
        lock_guard<mutex> lck(client_idx_lck_);
        tmp_lck = client_lck_idx_[client_id];
    }
    tmp_lck->unlock();
    return ;
}

/**
 * @brief check the file status
 * 
 * @param recipe_path the full recipe path
 * @param opt_type the opt type
 * @return true success
 * @return false fail
 */
bool ServerOptThd::CheckFileStat(string& recipe_path, int opt_type) {
    if (tool::FileExist(recipe_path)) {
        // the file exists
        switch (opt_type) {
            case UPLOAD_OPT: {
                tool::Logging(my_name_.c_str(), "%s exists, overwrite it.\n",
                    recipe_path.c_str());
                break;
            }
            case DOWNLOAD_OPT: {
                tool::Logging(my_name_.c_str(), "%s exists, access it.\n",
                    recipe_path.c_str());
                break;
            }
        }
    } else {
        switch (opt_type) {
            case UPLOAD_OPT: {
                tool::Logging(my_name_.c_str(), "%s not exists, create it.\n",
                    recipe_path.c_str());
                break;
            }
            case DOWNLOAD_OPT: {
                tool::Logging(my_name_.c_str(), "%s not exists, restore reject.\n",
                    recipe_path.c_str());
                return false;
            }
        }
    }
    return true;
}

/**
 * @brief load previous stat 
 * 
 */
void ServerOptThd::LoadStat() {
    if (tool::FileExist(persist_stat_name_)) {
        // the stat file exists
        ifstream prev_stat_file_hdl;
        prev_stat_file_hdl.open(persist_stat_name_, ios_base::binary |
            ios_base::in);
        if (!prev_stat_file_hdl.is_open()) {
            tool::Logging(my_name_.c_str(), "cannot open the stat file.\n");
            exit(EXIT_FAILURE);
        } else {
            // for dedup
            prev_stat_file_hdl.read(
                (char*)&data_recv_thd_->_total_logical_chunk_num,
                sizeof(uint64_t));
            prev_stat_file_hdl.read(
                (char*)&data_recv_thd_->_total_logical_data_size,
                sizeof(uint64_t));
            prev_stat_file_hdl.read(
                (char*)&data_recv_thd_->_total_unique_chunk_num,
                sizeof(uint64_t));
            prev_stat_file_hdl.read(
                (char*)&data_recv_thd_->_total_unique_data_size,
                sizeof(uint64_t));

            // for local delta compression
            prev_stat_file_hdl.read(
                (char*)&cache_comp_thd_->_total_similar_chunk_num,
                sizeof(uint64_t));
            prev_stat_file_hdl.read(
                (char*)&cache_comp_thd_->_total_similar_data_size,
                sizeof(uint64_t));
            prev_stat_file_hdl.read(
                (char*)&cache_comp_thd_->_total_delta_size,
                sizeof(uint64_t));

            // for global delta compression
            prev_stat_file_hdl.read(
                (char*)&data_writer_thd_->_total_similar_chunk_num,
                sizeof(uint64_t));
            prev_stat_file_hdl.read(
                (char*)&data_writer_thd_->_total_similar_data_size,
                sizeof(uint64_t));
            prev_stat_file_hdl.read(
                (char*)&data_writer_thd_->_total_delta_size,
                sizeof(uint64_t));

            // for local similarity histogram
            prev_stat_file_hdl.read(
                (char*)cache_comp_thd_->_similarity_hist,
                sizeof(uint64_t) * 7);
            // for global similarity histogram
            prev_stat_file_hdl.read(
                (char*)data_writer_thd_->_similarity_hist,
                sizeof(uint64_t) * 7);

            // for similarity reject count
            prev_stat_file_hdl.read(
                (char*)&cache_comp_thd_->_total_similarity_reject_num,
                sizeof(uint64_t));

            // for total stored data
            prev_stat_file_hdl.read((char*)&storage_core_->_write_chunk_num,
                sizeof(uint64_t));
            prev_stat_file_hdl.read((char*)&storage_core_->_write_data_size,
                sizeof(uint64_t));

            prev_stat_file_hdl.close();
        }
    }

    return ;
}

/**
 * @brief store current stat
 * 
 */
void ServerOptThd::StoreStat() {
    ofstream prev_stat_file_hdl;
    prev_stat_file_hdl.open(persist_stat_name_, ios_base::trunc);
    if (!prev_stat_file_hdl.is_open()) {
        tool::Logging(my_name_.c_str(), "cannot open the stat file.\n");
        exit(EXIT_FAILURE);
    } else {
        // for dedup
        prev_stat_file_hdl.write(
            (char*)&data_recv_thd_->_total_logical_chunk_num,
            sizeof(uint64_t));
        prev_stat_file_hdl.write(
            (char*)&data_recv_thd_->_total_logical_data_size,
            sizeof(uint64_t));
        prev_stat_file_hdl.write(
            (char*)&data_recv_thd_->_total_unique_chunk_num,
            sizeof(uint64_t));
        prev_stat_file_hdl.write(
            (char*)&data_recv_thd_->_total_unique_data_size,
            sizeof(uint64_t));
        
        // for local delta compression
        prev_stat_file_hdl.write(
            (char*)&cache_comp_thd_->_total_similar_chunk_num,
            sizeof(uint64_t));
        prev_stat_file_hdl.write(
            (char*)&cache_comp_thd_->_total_similar_data_size,
            sizeof(uint64_t));
        prev_stat_file_hdl.write(
            (char*)&cache_comp_thd_->_total_delta_size,
            sizeof(uint64_t));

        // for global delta compression
        prev_stat_file_hdl.write(
            (char*)&data_writer_thd_->_total_similar_chunk_num,
            sizeof(uint64_t));
        prev_stat_file_hdl.write(
            (char*)&data_writer_thd_->_total_similar_data_size,
            sizeof(uint64_t));
        prev_stat_file_hdl.write(
            (char*)&data_writer_thd_->_total_delta_size,
            sizeof(uint64_t));

        // for local similarity histogram
        prev_stat_file_hdl.write(
            (char*)cache_comp_thd_->_similarity_hist,
            sizeof(uint64_t) * 7);
        // for global similarity histogram
        prev_stat_file_hdl.write(
            (char*)data_writer_thd_->_similarity_hist,
            sizeof(uint64_t) * 7);

        // for similarity reject count
        prev_stat_file_hdl.write(
            (char*)&cache_comp_thd_->_total_similarity_reject_num,
            sizeof(uint64_t));

        // for total stored data
        prev_stat_file_hdl.write((char*)&storage_core_->_write_chunk_num,
            sizeof(uint64_t));
        prev_stat_file_hdl.write((char*)&storage_core_->_write_data_size,
            sizeof(uint64_t));
        
        prev_stat_file_hdl.close();
    }

    return ;
}

/**
 * @brief print the info of curClient
 * 
 * @param total_cache_size total cache size
 */
void ServerOptThd::PrintClientLog(uint64_t total_cache_size) {
    ofstream server_log_hdl;
    bool is_first_server_log = !(tool::FileExist(server_log_name_));
    server_log_hdl.open(server_log_name_, ios_base::app | ios_base::out);
    if (!server_log_hdl.is_open()) {
        tool::Logging(my_name_.c_str(), "cannot open the server log file.\n");
        exit(EXIT_FAILURE);
    } else {
        if (is_first_server_log) {
            server_log_hdl << "logical size, " << "logical chunk num, "
                << "unique size, " <<  "unique chunk num, "
                << "similar size (l), " << "similar chunk num (l), "
                << "delta size (l), " << "similar size (g), "
                << "similar chunk num (g), " << "delta size (g), "
                << "storage size, " << "similarity reject num, "
                << "cache size" << endl;
        }
        server_log_hdl << data_recv_thd_->_total_logical_data_size << ", "
            << data_recv_thd_->_total_logical_chunk_num << ", "
            << dual_dedup_thd_->_total_unique_data_size << ", "
            << dual_dedup_thd_->_total_unique_chunk_num << ", "
            << cache_comp_thd_->_total_similar_data_size << ", "
            << cache_comp_thd_->_total_similar_chunk_num << ", "
            << cache_comp_thd_->_total_delta_size << ", "
            << data_writer_thd_->_total_similar_data_size << ", "
            << data_writer_thd_->_total_similar_chunk_num << ", "
            << data_writer_thd_->_total_delta_size << ", "
            << storage_core_->_write_data_size << ", "
            << cache_comp_thd_->_total_similarity_reject_num << ", "
            << total_cache_size << endl;
        server_log_hdl.close();
    }

#ifdef EDR_BREAKDOWN
    string server_breakdown_file_name = "breakdown-server-file";
    ofstream server_breakdown_log_hdl;
    bool is_first_breakdown_log = !(tool::FileExist(server_breakdown_file_name));
    server_breakdown_log_hdl.open(server_breakdown_file_name, ios_base::app | ios_base::out);
    if (!server_breakdown_log_hdl.is_open()) {
        tool::Logging(my_name_.c_str(), "cannot open the server breakdown log file.\n");
        exit(EXIT_FAILURE);
    } else {
        if (is_first_breakdown_log) {
            server_breakdown_log_hdl << "cipher fp size, " << "cipher fp time, "
                << "dual fp size, " << "dual fp time, "
                << "dedup time, " << "cipher comp feature size, "
                << "cipher comp feature time, " << "cache manage size, "
                << "cache manage time, " << "cache delta size, "
                << "cache delta time, " << "comp delta size, "
                << "comp delta time" << endl;
        }
        server_breakdown_log_hdl << data_recv_thd_->_total_cipher_fp_data_size << ", "
            << data_recv_thd_->_total_cipher_fp_time << ", "
            << data_recv_thd_->_total_dual_fp_data_size << ", "
            << data_recv_thd_->_total_dual_fp_time << ", "
            << dual_dedup_thd_->_total_dedup_time << ", "
            << dual_dedup_thd_->_total_cipher_feature_data_size << ", "
            << dual_dedup_thd_->_total_cipher_feature_time << ", "
            << cache_comp_thd_->_total_cache_manage_data_size << ", "
            << cache_comp_thd_->_total_cache_manage_time << ", "
            << cache_comp_thd_->_total_cache_delta_data_size << ", "
            << cache_comp_thd_->_total_cache_delta_time << ", "
            << data_writer_thd_->_total_comp_delta_data_size << ", "
            << data_writer_thd_->_total_comp_delta_time << endl;
        server_breakdown_log_hdl.close();
    }
#endif

    return ;
}
