/**
 * @file rocksdb_db.cc
 * @author Zuoru YANG (zryang@cse.cuhk.edu.hk)
 * @brief implement the interfaces of RocksdbDatabase
 * @version 0.1
 * @date 2022-08-19
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include "../../include/database/rocksdb_db.h"

/**
 * @brief Construct a new RocksdbDatabase object
 * 
 * @param db_name the path of the db file
 */
RocksdbDatabase::RocksdbDatabase(string db_name) {
    db_name_ = db_name;
    this->OpenDB(db_name);
}

/**
 * @brief Destroy the RocksdbDatabase object
 * 
 */
RocksdbDatabase::~RocksdbDatabase() {
    delete rocks_db_obj_;
}

/**
 * @brief open a database
 * 
 * @param db_name the db path
 * @return true success
 * @return false fail
 */
bool RocksdbDatabase::OpenDB(string db_name) {
    // global option
    options_.create_if_missing = true;
    options_.IncreaseParallelism(16);
    options_.OptimizeLevelStyleCompaction();
    // OptimizeForPointLookup takes MiB.  4096 therefore allowed a 4 GiB
    // cache for every short-lived InformCache database.
    options_.OptimizeForPointLookup(256);
    options_.db_write_buffer_size = static_cast<size_t>(64) << 20;
    options_.max_write_buffer_number = 2;
    options_.min_write_buffer_number_to_merge = 1;
    options_.info_log_level = static_cast<rocksdb::InfoLogLevel>(4); // FATAL_LEVEL
    options_.compression = rocksdb::CompressionType::kNoCompression;

    // write option
    write_options_ = rocksdb::WriteOptions();
    write_options_.disableWAL = true;

    // read option
    read_options_ = rocksdb::ReadOptions();

    rocksdb::Status status = rocksdb::DB::Open(options_, db_name_,
        &this->rocks_db_obj_);
    assert(status.ok());
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
bool RocksdbDatabase::Query(const string& key, string& value) {
    rocksdb::Status query_stat = rocks_db_obj_->Get(read_options_, key, &value);
    return query_stat.ok();
}

/**
 * @brief insert the (key, value) pair
 * 
 * @param key key
 * @param value value
 * @return true success
 * @return false fail
 */
bool RocksdbDatabase::Insert(const string& key, const string& value) {
    rocksdb::Status insert_stat = rocks_db_obj_->Put(write_options_, key, value); 
    return insert_stat.ok();
}

/**
 * @brief insert the (key, value) pair
 * 
 * @param key the key 
 * @param buf the value buffer
 * @param buf_size the buffer size
 * @return true success
 * @return false fail
 */
bool RocksdbDatabase::InsertBuffer(const string& key, const char* buf, size_t buf_size) {
    rocksdb::Status insert_stat = rocks_db_obj_->Put(write_options_,
        key, rocksdb::Slice(buf, buf_size));
    return insert_stat.ok();
}

/**
 * @brief insert the (key, value) pair
 * 
 * @param key the key
 * @param key_size the key size
 * @param buf the value buffer
 * @param buf_size the buffer size
 * @return true success
 * @return false fail
 */
bool RocksdbDatabase::InsertBothBuffer(const char* key, size_t key_size, const char* buf,
    size_t buf_size) {
    rocksdb::Status insert_stat = rocks_db_obj_->Put(write_options_,
        rocksdb::Slice(key, key_size), rocksdb::Slice(buf, buf_size));
    return insert_stat.ok();
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
bool RocksdbDatabase::QueryBuffer(const char* key, size_t key_size, string& value) {
    rocksdb::Status query_stat = rocks_db_obj_->Get(read_options_,
        rocksdb::Slice(key, key_size), &value);
    return query_stat.ok();
}

vector<bool> RocksdbDatabase::BatchQueryBuffer(const vector<string>& keys,
    vector<string>& values) {
    vector<rocksdb::Slice> key_slices;
    key_slices.reserve(keys.size());
    for (const string& key : keys) {
        key_slices.emplace_back(key.data(), key.size());
    }

    const vector<rocksdb::Status> statuses = rocks_db_obj_->MultiGet(
        read_options_, key_slices, &values);
    vector<bool> found(keys.size(), false);
    for (size_t key_idx = 0; key_idx < statuses.size(); key_idx++) {
        found[key_idx] = statuses[key_idx].ok();
    }
    return found;
}

map<size_t, uint64_t> RocksdbDatabase::GetValueItemCountStat(
    size_t item_size) {
    map<size_t, uint64_t> stat;
    if (item_size == 0) {
        return stat;
    }

    rocksdb::ReadOptions scan_options = read_options_;
    scan_options.fill_cache = false;
    rocksdb::Iterator* it = rocks_db_obj_->NewIterator(scan_options);
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        const size_t value_size = it->value().size();
        if (value_size == 0 || value_size % item_size != 0) {
            stat[0]++;
        } else {
            stat[value_size / item_size]++;
        }
    }
    if (!it->status().ok()) {
        tool::Logging(my_name_.c_str(),
            "iterate database for value statistics failed.\n");
    }
    delete it;
    return stat;
}

/**
 * @brief delete a given key
 * 
 * @param key key ptr
 * @param key_size key size
 */
void RocksdbDatabase::DeleteBuffer(const char* key, size_t key_size) {
    rocks_db_obj_->Delete(write_options_, rocksdb::Slice(key, key_size));
    return ;
}

/**
 * @brief delete a given key
 * 
 * @param key key str
 */
void RocksdbDatabase::Delete(const string& key) {
    rocks_db_obj_->Delete(write_options_, key);
    return ;
}
