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

#include <filesystem>
#include <jemalloc/jemalloc.h>

#define EDRSTORE_STRINGIFY_IMPL(value) #value
#define EDRSTORE_STRINGIFY(value) EDRSTORE_STRINGIFY_IMPL(value)

/**
 * @brief Construct a new ServerOptThd object
 * 
 * @param server_channel the server channel
 * @param fp_2_addr_db the fp to address index
 * @param binary_tree_index persistent similarity candidate index
 */
ServerOptThd::ServerOptThd(SSLConnection* server_channel, 
    AbsDatabase* fp_2_addr_db, BinaryTreeIndex* binary_tree_index) {
    server_channel_ = server_channel;
    fp_2_addr_db_ = fp_2_addr_db;
    binary_tree_index_ = binary_tree_index;

    // init the upload
    storage_core_ = new StorageCore();
    
    data_recv_thd_ = new DataRecvThd(server_channel_, fp_2_addr_db_);
    cache_comp_thd_ = new CacheCompThd();
    data_writer_thd_ = new DataWriterThd(fp_2_addr_db_, storage_core_,
        binary_tree_index_);
    dual_dedup_thd_ = new DualDedupThd(fp_2_addr_db_);
    
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
    const RecipeSpaceStat recipe_space = this->CollectRecipeSpace();
    const BinaryTreeIndexStat tree_stat = binary_tree_index_->GetStat();
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
    fprintf(stderr, "--------Recipe Space--------\n");
    fprintf(stderr, "file recipe files: %lu\n", recipe_space.file_recipe_count);
    fprintf(stderr, "file recipe size:  %lu B (%.2lf MiB)\n",
        recipe_space.file_recipe_bytes,
        static_cast<double>(recipe_space.file_recipe_bytes) / 1024.0 / 1024.0);
    fprintf(stderr, "key recipe files:  %lu\n", recipe_space.key_recipe_count);
    fprintf(stderr, "key recipe size:   %lu B (%.2lf MiB)\n",
        recipe_space.key_recipe_bytes,
        static_cast<double>(recipe_space.key_recipe_bytes) / 1024.0 / 1024.0);
    fprintf(stderr, "total recipe size: %lu B (%.2lf MiB)\n",
        recipe_space.file_recipe_bytes + recipe_space.key_recipe_bytes,
        static_cast<double>(recipe_space.file_recipe_bytes +
            recipe_space.key_recipe_bytes) / 1024.0 / 1024.0);
    fprintf(stderr, "container + file recipe: %lu B\n",
        storage_core_->_write_data_size + recipe_space.file_recipe_bytes);
    fprintf(stderr, "container + all recipes: %lu B\n",
        storage_core_->_write_data_size + recipe_space.file_recipe_bytes +
            recipe_space.key_recipe_bytes);
    fprintf(stderr, "--------Binary Tree Index--------\n");
    fprintf(stderr, "tree depth: %u\n", BINARY_TREE_INDEX_DEPTH);
    fprintf(stderr, "tree paths per chunk limit: %lu\n",
        static_cast<uint64_t>(MAX_TREE_PATHS));
    fprintf(stderr, "candidate limit per leaf/chunk: %lu/%lu\n",
        static_cast<uint64_t>(MAX_CANDIDATES_PER_LEAF),
        static_cast<uint64_t>(MAX_BINARY_TREE_CANDIDATES_PER_CHUNK));
    fprintf(stderr, "materialized nodes: %lu\n",
        tree_stat.materialized_node_num);
    fprintf(stderr, "leaf buckets/entries: %lu/%lu\n",
        tree_stat.leaf_bucket_num, tree_stat.leaf_entry_num);
    fprintf(stderr, "indexed chunks/file paths: %lu/%lu\n",
        tree_stat.indexed_chunk_num, tree_stat.file_path_num);
    fprintf(stderr, "candidate searches/leaves/verified: %lu/%lu/%lu\n",
        tree_stat.candidate_search_num, tree_stat.candidate_leaf_num,
        tree_stat.candidate_verify_num);
    fprintf(stderr, "Similarity Compare Count: %lu\n",
        data_writer_thd_->GetSimilarityCompareCount());
    fprintf(stderr, "Total Similarity Detection Time(ms): %.3lf\n",
        data_writer_thd_->GetSimilarityDetectionTimeMs());
    fprintf(stderr, "BinaryTree Search Time(ms): %.3lf\n",
        static_cast<double>(tree_stat.binary_tree_search_time_us) / 1000.0);
    fprintf(stderr, "Candidate Filtering Time(ms): %.3lf\n",
        static_cast<double>(tree_stat.candidate_filtering_time_us) / 1000.0);
    fprintf(stderr, "Similarity Compare Time(ms): %.3lf\n",
        data_writer_thd_->GetSimilarityCompareTimeMs());
    fprintf(stderr, "=================================\n");

    // Keep all counters alive until the final report has been emitted.
    delete storage_core_;
    delete data_recv_thd_;
    delete cache_comp_thd_;
    delete data_writer_thd_;
    delete dual_dedup_thd_;
    delete data_reader_thd_;
    delete data_decode_thd_;

    for (auto it : client_lck_idx_) {
        delete it.second;
    }
}

ServerOptThd::RecipeSpaceStat ServerOptThd::CollectRecipeSpace() const {
    RecipeSpaceStat result;
    const std::string file_suffix = config.GetRecipeSuffix();
    const std::string key_suffix = config.GetKeyRecipeSuffix();
    std::error_code error;
    std::filesystem::directory_iterator iterator(
        config.GetRecipeRootPath(), error);
    const std::filesystem::directory_iterator end;

    while (!error && iterator != end) {
        const std::filesystem::directory_entry& entry = *iterator;
        std::error_code entry_error;
        if (entry.is_regular_file(entry_error) && !entry_error) {
            const std::string name = entry.path().filename().string();
            const uint64_t bytes = entry.file_size(entry_error);
            if (!entry_error) {
                if (name.size() >= file_suffix.size() &&
                    name.compare(name.size() - file_suffix.size(),
                        file_suffix.size(), file_suffix) == 0) {
                    result.file_recipe_bytes += bytes;
                    result.file_recipe_count++;
                } else if (name.size() >= key_suffix.size() &&
                    name.compare(name.size() - key_suffix.size(),
                        key_suffix.size(), key_suffix) == 0) {
                    result.key_recipe_bytes += bytes;
                    result.key_recipe_count++;
                }
            }
        }
        iterator.increment(error);
    }
    return result;
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
    uint64_t total_cache_size = 0;
    cur_client->_total_cache_size = &total_cache_size;
    delete cur_client;
    // Return pages retained by request worker arenas after their caches and
    // queues have been destroyed. This does not change cache semantics.
    mallctl("arena." EDRSTORE_STRINGIFY(MALLCTL_ARENAS_ALL) ".purge",
        nullptr, nullptr, nullptr, 0);
    if (opt_type == UPLOAD_OPT) {
        // The recipe is closed by ClientVar destruction before it is scanned.
        binary_tree_index_->RecordFileRecipe(file_name, recipe_path);
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
    const RecipeSpaceStat recipe_space = this->CollectRecipeSpace();
    const BinaryTreeIndexStat tree_stat = binary_tree_index_->GetStat();
    const double average_candidate_num = tree_stat.candidate_search_num == 0 ?
        0.0 : static_cast<double>(tree_stat.candidate_returned_num) /
            tree_stat.candidate_search_num;
    const double average_scanned_leaf_entries =
        tree_stat.candidate_search_num == 0 ? 0.0 :
            static_cast<double>(tree_stat.loaded_leaf_entry_num) /
                tree_stat.candidate_search_num;
    const double average_tree_search_num =
        tree_stat.candidate_search_num == 0 ? 0.0 :
            static_cast<double>(tree_stat.tree_path_search_num) /
                tree_stat.candidate_search_num;
    const double average_leaf_size = tree_stat.candidate_leaf_num == 0 ?
        0.0 : static_cast<double>(tree_stat.loaded_leaf_entry_num) /
            tree_stat.candidate_leaf_num;
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
                << "storage size, cache size, "
                << "file recipe size, key recipe size, total recipe size, "
                << "average candidates per chunk, max candidates per chunk, "
                << "average leaf size, max leaf size, "
                << "similarity compare count, similarity detection time(ms), "
                << "average scanned leaf entries, candidate verified, "
                << "binary tree search time(ms), candidate filtering time(ms), "
                << "similarity compare time(ms), average tree searches, "
                << "leaf cache hits, leaf cache misses, leaf cache entries, "
                << "leaf cache current bytes, leaf cache peak bytes, "
                << "leaf cache limit bytes, leaf cache evictions"
                << endl;
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
            << total_cache_size << ", "
            << recipe_space.file_recipe_bytes << ", "
            << recipe_space.key_recipe_bytes << ", "
            << recipe_space.file_recipe_bytes + recipe_space.key_recipe_bytes
            << ", " << fixed << setprecision(3) << average_candidate_num
            << ", " << tree_stat.max_candidate_num
            << ", " << average_leaf_size
            << ", " << tree_stat.max_loaded_leaf_size
            << ", " << data_writer_thd_->GetSimilarityCompareCount()
            << ", "
            << data_writer_thd_->GetSimilarityDetectionTimeMs()
            << ", " << average_scanned_leaf_entries
            << ", " << tree_stat.candidate_verify_num
            << ", " << static_cast<double>(
                tree_stat.binary_tree_search_time_us) / 1000.0
            << ", " << static_cast<double>(
                tree_stat.candidate_filtering_time_us) / 1000.0
            << ", " << data_writer_thd_->GetSimilarityCompareTimeMs()
            << ", " << average_tree_search_num
            << ", " << tree_stat.leaf_cache_hit_num
            << ", " << tree_stat.leaf_cache_miss_num
            << ", " << tree_stat.leaf_cache_entry_num
            << ", " << tree_stat.leaf_cache_current_bytes
            << ", " << tree_stat.leaf_cache_peak_bytes
            << ", " << tree_stat.leaf_cache_limit_bytes
            << ", " << tree_stat.leaf_cache_eviction_num
            << endl;
        tool::Logging(my_name_.c_str(),
            "BinaryTree Candidate Statistics:\n"
            "average scanned leaf entries: %.3lf\n"
            "average selected candidates: %.3lf\n"
            "candidate verified: %lu\n"
            "average candidate number per chunk: %.3lf\n"
            "max candidate number per chunk: %lu\n"
            "average leaf size: %.3lf\n"
            "max leaf size: %lu\n"
            "average tree search number per chunk: %.3lf\n"
            "leaf cache hit/miss: %lu/%lu\n"
            "leaf cache entries: %lu\n"
            "leaf cache bytes current/peak/limit: %lu/%lu/%lu\n"
            "leaf cache evictions: %lu\n",
            average_scanned_leaf_entries, average_candidate_num,
            tree_stat.candidate_verify_num, average_candidate_num,
            tree_stat.max_candidate_num, average_leaf_size,
            tree_stat.max_loaded_leaf_size, average_tree_search_num,
            tree_stat.leaf_cache_hit_num, tree_stat.leaf_cache_miss_num,
            tree_stat.leaf_cache_entry_num,
            tree_stat.leaf_cache_current_bytes,
            tree_stat.leaf_cache_peak_bytes,
            tree_stat.leaf_cache_limit_bytes,
            tree_stat.leaf_cache_eviction_num);
        tool::Logging(my_name_.c_str(),
            "Similarity Compare Count: %lu\n",
            data_writer_thd_->GetSimilarityCompareCount());
        tool::Logging(my_name_.c_str(),
            "Total Similarity Detection Time(ms): %.3lf\n",
            data_writer_thd_->GetSimilarityDetectionTimeMs());
        tool::Logging(my_name_.c_str(),
            "BinaryTree Search Time(ms): %.3lf\n"
            "Candidate Filtering Time(ms): %.3lf\n"
            "Similarity Compare Time(ms): %.3lf\n",
            static_cast<double>(tree_stat.binary_tree_search_time_us) / 1000.0,
            static_cast<double>(tree_stat.candidate_filtering_time_us) / 1000.0,
            data_writer_thd_->GetSimilarityCompareTimeMs());
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
