/**
 * @file in_mem_db.cc
 * @author Zuoru YANG (zryang@cse.cuhk.edu.hk)
 * @brief implement the interface of in-memory index
 *        [MODIFIED] 原哈希表实现已注释，替换为论文中的满二叉树存储方案
 *        Full binary tree: leaf nodes store block tags (fp),
 *        O(1) hash map tag→leaf position, O(1) position→addr.
 * @version 0.1
 * @date 2021-05-05
 *
 * @copyright Copyright (c) 2021
 *
 */

#include "../../include/database/in_mem_db.h"

/**
 * @brief Construct a new In Memory Database object
 *
 * @param db_name the path of the db file
 */
InMemoryDatabase::InMemoryDatabase(string db_name) {
    // ==================== 原哈希表初始化（已注释）====================
    // this->OpenDB(db_name);
    // pthread_rwlock_init(&rwlock_, NULL);
    // ==================== 二叉树存储初始化 ====================
    tree_index_ = new BinaryTreeIndex(10); // 初始高度10 → 1024个叶节点
    uint32_t total_nodes = (uint32_t(1) << (10 + 1));
    addr_store_.resize(total_nodes);
    pthread_rwlock_init(&rwlock_, NULL);
    pthread_rwlock_init(&addr_rwlock_, NULL);
    this->OpenDB(db_name);
}

/**
 * @brief Destroy the In Memory Database object
 *
 */
InMemoryDatabase::~InMemoryDatabase() {
    // ==================== 原持久化逻辑（已注释）====================
    // // persistent the indexFile to the disk
    // ofstream db_file;
    // db_file.open(db_name_, ios_base::trunc | ios_base::binary);
    // uint32_t item_size = 0;
    // for (auto it = index_obj_.begin(); it != index_obj_.end(); it++) {
    //     // write the key
    //     item_size = it->first.size();
    //     db_file.write((char*)&item_size, sizeof(uint32_t));
    //     db_file.write(it->first.c_str(), item_size);
    //
    //     // write the value
    //     item_size = it->second.size();
    //     db_file.write((char*)&item_size, sizeof(uint32_t));
    //     db_file.write(it->second.c_str(), item_size);
    // }
    // db_file.close();
    // pthread_rwlock_destroy(&rwlock_);
    // ==================== 二叉树持久化 ====================
    // persist binary tree index to disk
    ofstream db_file;
    db_file.open(db_name_, ios_base::trunc | ios_base::binary);
    if (db_file.is_open()) {
        uint32_t item_size = 0;
        uint32_t leaf_base = tree_index_->GetLeafBase();
        uint32_t leaf_end = leaf_base + tree_index_->GetNumLeaves();
        for (uint32_t i = leaf_base; i < leaf_end; i++) {
            if (!addr_store_[i].empty()) {
                // write leaf position
                db_file.write((char*)&i, sizeof(uint32_t));
                // write tag (fp)
                db_file.write((char*)tree_index_->GetTree()[i].tag, CHUNK_HASH_SIZE);
                // write value (addr)
                item_size = addr_store_[i].size();
                db_file.write((char*)&item_size, sizeof(uint32_t));
                db_file.write(addr_store_[i].c_str(), item_size);
            }
        }
        // end marker
        uint32_t end_marker = 0;
        db_file.write((char*)&end_marker, sizeof(uint32_t));
        db_file.close();
    }
    pthread_rwlock_destroy(&rwlock_);
    pthread_rwlock_destroy(&addr_rwlock_);
    delete tree_index_;
}

/**
 * @brief open a database
 *
 * @param db_name the db path
 * @return true success
 * @return false fail
 */
bool InMemoryDatabase::OpenDB(string db_name) {
    db_name_ = db_name;
    // ==================== 原加载逻辑（已注释）====================
    // // check whether there exists the index
    // ifstream db_file;
    // db_file.open(db_name_, ios_base::in | ios_base::binary);
    // if (!db_file.is_open()) {
    //     tool::Logging(my_name_.c_str(), "cannot open the db file.\n");
    // }
    //
    // size_t start_size = db_file.tellg();
    // db_file.seekg(0, ios_base::end);
    // size_t fileSize = db_file.tellg();
    // fileSize = fileSize - start_size;
    //
    // if (fileSize == 0) {
    //     // db file not exist
    //     tool::Logging(my_name_.c_str(), "db file file not exist, create a new one.\n");
    // } else {
    //     // db file exist, load
    //     db_file.seekg(0, ios_base::beg);
    //     bool is_end = false;
    //     uint32_t item_size = 0;
    //     string key;
    //     string value;
    //     while (!is_end) {
    //         // read key
    //         db_file.read((char*)&item_size, sizeof(uint32_t));
    //         if (item_size == 0) {
    //             break;
    //         }
    //         key.resize(item_size, 0);
    //         db_file.read((char*)&key[0], item_size);
    //
    //         // read value
    //         db_file.read((char*)&item_size, sizeof(uint32_t));
    //         value.resize(item_size, 0);
    //         db_file.read((char*)&value[0], item_size);
    //         is_end = db_file.eof();
    //
    //         // update the index
    //         index_obj_[key] = value;
    //         item_size = 0;
    //
    //         if (is_end) {
    //             break;
    //         }
    //     }
    // }
    // db_file.close();
    // tool::Logging(my_name_.c_str(), "loaded index size: %lu\n",
    //     index_obj_.size());
    // return true;
    // ==================== 二叉树加载逻辑 ====================
    ifstream db_file;
    db_file.open(db_name_, ios_base::in | ios_base::binary);
    if (!db_file.is_open()) {
        tool::Logging(my_name_.c_str(), "cannot open db file, fresh start.\n");
        return true;
    }

    size_t start_size = db_file.tellg();
    db_file.seekg(0, ios_base::end);
    size_t fileSize = db_file.tellg();
    fileSize = fileSize - start_size;

    if (fileSize == 0) {
        tool::Logging(my_name_.c_str(), "db file not exist, create a new one.\n");
        db_file.close();
        return true;
    }

    // db file exists, load into binary tree
    db_file.seekg(0, ios_base::beg);
    uint32_t pos = 0;
    uint32_t item_size = 0;
    uint32_t load_count = 0;

    while (true) {
        db_file.read((char*)&pos, sizeof(uint32_t));
        if (pos == 0) break;  // end marker

        // read tag (fingerprint)
        uint8_t tag[CHUNK_HASH_SIZE];
        db_file.read((char*)tag, CHUNK_HASH_SIZE);

        // read value (address data)
        db_file.read((char*)&item_size, sizeof(uint32_t));
        string value;
        value.resize(item_size, 0);
        db_file.read((char*)&value[0], item_size);

        // insert into binary tree: fp → leaf position (tree allocates position)
        uint32_t allocated_pos = tree_index_->InsertBlockTag(tag, CHUNK_HASH_SIZE);
        // ensure addr_store_ is large enough for the allocated position
        if (allocated_pos >= addr_store_.size()) {
            addr_store_.resize(allocated_pos + 1024);
        }
        // store address at the allocated leaf position: position → addr
        addr_store_[allocated_pos] = value;

        load_count++;
    }

    db_file.close();
    tool::Logging(my_name_.c_str(), "loaded %u entries into binary tree index (height=%u, leaves=%u).\n",
        load_count, tree_index_->GetMaxHeight(), tree_index_->GetLeafCapacity());
    return true;
}

/**
 * @brief execute query over database
 *
 * @param key key
 * @param value value
 * @return true exist
 * @return false not exist
 */
bool InMemoryDatabase::Query(const string& key, string& value) {
    // ==================== 原哈希表查询（已注释）====================
    // pthread_rwlock_rdlock(&rwlock_);
    // bool ret;
    // auto find_ret = index_obj_.find(key);
    // if (find_ret != index_obj_.end()) {
    //     // it exists in the index
    //     value.assign(find_ret->second);
    //     ret = true;
    // } else {
    //     // it does not exist
    //     ret = false;
    // }
    // pthread_rwlock_unlock(&rwlock_);
    // return ret;
    // ==================== 二叉树查询（rwlock_ 保护完整操作，消除 TOCTOU）====================
    pthread_rwlock_rdlock(&rwlock_);
    uint32_t pos = tree_index_->FindLeafPosition((const uint8_t*)key.c_str(), key.size());
    if (pos == 0) {
        pthread_rwlock_unlock(&rwlock_);
        return false;
    }
    if (pos < addr_store_.size() && !addr_store_[pos].empty()) {
        value = addr_store_[pos];
        pthread_rwlock_unlock(&rwlock_);
        return true;
    }
    pthread_rwlock_unlock(&rwlock_);
    return false;
}

/**
 * @brief insert the (key, value) pair
 *
 * @param key key
 * @param value value
 * @return true exist
 * @return false not exist
 */
bool InMemoryDatabase::Insert(const string& key, const string& value) {
    // ==================== 原哈希表插入（已注释）====================
    // pthread_rwlock_wrlock(&rwlock_);
    // index_obj_[key] = value;
    // pthread_rwlock_unlock(&rwlock_);
    // return true;
    // ==================== 二叉树插入 ====================
    return InsertBothBuffer(key.c_str(), key.size(), value.c_str(), value.size());
}

/**
 * @brief insert the (key, value) pair
 *
 * @param key the key
 * @param buf the value buffer
 * @param buf_size the buffer size
 * @return true exist
 * @return false not exist
 */
bool InMemoryDatabase::InsertBuffer(const string& key, const char* buf, size_t buf_size) {
    // ==================== 原哈希表插入（已注释）====================
    // pthread_rwlock_wrlock(&rwlock_);
    // string value_str;
    // value_str.assign(buf, buf_size);
    // index_obj_[key] = value_str;
    // pthread_rwlock_unlock(&rwlock_);
    // return true;
    // ==================== 二叉树插入 ====================
    return InsertBothBuffer(key.c_str(), key.size(), buf, buf_size);
}

/**
 * @brief insert the (key, value) pair
 *
 * @param key the key
 * @param key_size the key size
 * @param buf the value buffer
 * @param buf_size the buffer size
 * @return true exist (already existed)
 * @return false not exist (newly inserted)
 */
bool InMemoryDatabase::InsertBothBuffer(const char* key, size_t key_size, const char* buf,
    size_t buf_size) {
    // ==================== 原哈希表插入（已注释）====================
    // pthread_rwlock_wrlock(&rwlock_);
    // string key_str;
    // string value_str;
    // key_str.assign(key, key_size);
    // value_str.assign(buf, buf_size);
    // index_obj_[key_str] = value_str;
    // pthread_rwlock_unlock(&rwlock_);
    // return true;
    // ==================== 二叉树插入（rwlock_ 保护完整操作，消除 TOCTOU）====================
    // rwlock_ 写锁确保：InsertBlockTag 写哈希映射 + addr_store_ 写地址
    // 作为一个原子操作完成，其他线程的查询不会看到中间态
    pthread_rwlock_wrlock(&rwlock_);
    uint32_t pos = tree_index_->InsertBlockTag((const uint8_t*)key, key_size);

    if (pos >= addr_store_.size()) {
        addr_store_.resize(pos + 1024);
    }
    bool already_existed = !addr_store_[pos].empty();
    addr_store_[pos].assign(buf, buf_size);
    pthread_rwlock_unlock(&rwlock_);

    return already_existed;
}

/**
 * @brief query the (key, value) pair
 *
 * @param key the key
 * @param key_size the key size
 * @param value the value
 * @return true exist
 * @return false not exist
 */
bool InMemoryDatabase::QueryBuffer(const char* key, size_t key_size, string& value) {
    // ==================== 原哈希表查询（已注释）====================
    // pthread_rwlock_rdlock(&rwlock_);
    // string key_str;
    // bool ret;
    // key_str.assign(key, key_size);
    // auto find_ret = index_obj_.find(key_str);
    // if (find_ret != index_obj_.end()) {
    //     // it exists in the index
    //     value.assign(find_ret->second);
    //     ret = true;
    // } else {
    //     // it does not exist
    //     ret = false;
    // }
    // pthread_rwlock_unlock(&rwlock_);
    // return ret;
    // ==================== 二叉树查询（rwlock_ 保护完整操作，消除 TOCTOU）====================
    pthread_rwlock_rdlock(&rwlock_);
    uint32_t pos = tree_index_->FindLeafPosition((const uint8_t*)key, key_size);
    if (pos == 0) {
        pthread_rwlock_unlock(&rwlock_);
        return false;
    }

    if (pos < addr_store_.size() && !addr_store_[pos].empty()) {
        value = addr_store_[pos];
        pthread_rwlock_unlock(&rwlock_);
        return true;
    }
    pthread_rwlock_unlock(&rwlock_);
    return false;
}

/**
 * @brief delete a given key
 *
 * @param key key ptr
 * @param key_size key size
 */
void InMemoryDatabase::DeleteBuffer(const char* key, size_t key_size) {
    // ==================== 原哈希表删除（已注释）====================
    // string key_str;
    // key_str.assign(key, key_size);
    // index_obj_.erase(key_str);
    // return ;
    // ==================== 二叉树删除（rwlock_ 保护完整操作）====================
    pthread_rwlock_wrlock(&rwlock_);
    uint32_t pos = tree_index_->FindLeafPosition((const uint8_t*)key, key_size);
    if (pos != 0 && pos < addr_store_.size()) {
        addr_store_[pos].clear();
    }
    pthread_rwlock_unlock(&rwlock_);
    return ;
}

/**
 * @brief delete a given key
 *
 * @param key key str
 */
void InMemoryDatabase::Delete(const string& key) {
    // ==================== 原哈希表删除（已注释）====================
    // index_obj_.erase(key);
    // return ;
    // ==================== 二叉树删除 ====================
    DeleteBuffer(key.c_str(), key.size());
    return ;
}
