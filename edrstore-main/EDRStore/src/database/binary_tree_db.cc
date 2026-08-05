/**
 * @file binary_tree_db.cc
 * @brief Implement BinaryTreeDB backed by full binary tree index
 *
 * Reference: CSP full binary tree for secure deduplication
 *
 * @version 0.1
 * @date 2023
 */

#include "../../include/database/binary_tree_db.h"

/**
 * @brief Construct a new BinaryTreeDB object
 */
BinaryTreeDB::BinaryTreeDB(string db_name, uint32_t initial_height) {
    tree_index_ = new BinaryTreeIndex(initial_height);
    // addr_store_ is 1-indexed, resize to match tree capacity
    uint32_t total_nodes = (uint32_t(1) << (initial_height + 1));
    addr_store_.resize(total_nodes);
    pthread_rwlock_init(&rwlock_, NULL);
    pthread_rwlock_init(&addr_rwlock_, NULL);
    this->OpenDB(db_name);
}

/**
 * @brief Destroy
 */
BinaryTreeDB::~BinaryTreeDB() {
    // persist index to disk
    ofstream db_file;
    db_file.open(db_name_, ios_base::trunc | ios_base::binary);
    if (db_file.is_open()) {
        uint32_t item_size = 0;
        // iterate over all occupied leaves via the tree
        uint32_t leaf_base = tree_index_->GetLeafBase();
        uint32_t leaf_end = leaf_base + tree_index_->GetNumLeaves();
        for (uint32_t i = leaf_base; i < leaf_end; i++) {
            if (!addr_store_[i].empty()) {
                // write leaf position
                db_file.write((char*)&i, sizeof(uint32_t));
                // write tag
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
 * @brief Open / load the database from file
 */
bool BinaryTreeDB::OpenDB(string db_name) {
    db_name_ = db_name;
    ifstream db_file;
    db_file.open(db_name_, ios_base::in | ios_base::binary);
    if (!db_file.is_open()) {
        tool::Logging(my_name_.c_str(), "db file not exist, fresh start.\n");
        return true;
    }

    size_t start_size = db_file.tellg();
    db_file.seekg(0, ios_base::end);
    size_t fileSize = db_file.tellg();
    fileSize = fileSize - start_size;

    if (fileSize == 0) {
        tool::Logging(my_name_.c_str(), "empty db file, fresh start.\n");
        db_file.close();
        return true;
    }

    // load from file
    db_file.seekg(0, ios_base::beg);
    uint32_t pos = 0;
    uint32_t item_size = 0;
    uint32_t load_count = 0;

    while (true) {
        db_file.read((char*)&pos, sizeof(uint32_t));
        if (pos == 0) break;  // end marker

        // ensure addr_store_ is large enough
        if (pos >= addr_store_.size()) {
            addr_store_.resize(pos + 1024);
        }

        // read tag
        uint8_t tag[CHUNK_HASH_SIZE];
        db_file.read((char*)tag, CHUNK_HASH_SIZE);

        // read value
        db_file.read((char*)&item_size, sizeof(uint32_t));
        string value;
        value.resize(item_size, 0);
        db_file.read((char*)&value[0], item_size);

        // insert into tree (tree allocates leaf position)
        uint32_t allocated_pos = tree_index_->InsertBlockTag(tag, CHUNK_HASH_SIZE);
        // ensure addr_store_ is large enough
        if (allocated_pos >= addr_store_.size()) {
            addr_store_.resize(allocated_pos + 1024);
        }
        // store address at allocated leaf position
        addr_store_[allocated_pos] = value;

        load_count++;
    }

    db_file.close();
    tool::Logging(my_name_.c_str(), "loaded %u entries from db file.\n", load_count);
    return true;
}

/**
 * @brief Query by string key (fp → addr)
 */
bool BinaryTreeDB::Query(const string& key, string& value) {
    return QueryBuffer(key.c_str(), key.size(), value);
}

/**
 * @brief Insert by string key (fp, addr)
 */
bool BinaryTreeDB::Insert(const string& key, const string& value) {
    return InsertBothBuffer(key.c_str(), key.size(), value.c_str(), value.size());
}

/**
 * @brief Insert by key string + value buffer
 */
bool BinaryTreeDB::InsertBuffer(const string& key, const char* buf, size_t buf_size) {
    return InsertBothBuffer(key.c_str(), key.size(), buf, buf_size);
}

/**
 * @brief Insert (key buffer, value buffer) — core insert path
 *        key = fingerprint, value = address (KeyForChunkHashDB_t)
 */
bool BinaryTreeDB::InsertBothBuffer(const char* key, size_t key_size,
    const char* buf, size_t buf_size) {
    // rwlock_ wrlock ensures InsertBlockTag + addr_store_ write is atomic
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
 * @brief Query by key buffer (fp) → returns address
 *        Uses O(1) hash map lookup in BinaryTreeIndex
 */
bool BinaryTreeDB::QueryBuffer(const char* key, size_t key_size, string& value) {
    // rwlock_ rdlock ensures atomic read across tree hash map + addr_store_
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
 * @brief Delete a key
 */
void BinaryTreeDB::DeleteBuffer(const char* key, size_t key_size) {
    pthread_rwlock_wrlock(&rwlock_);
    uint32_t pos = tree_index_->FindLeafPosition((const uint8_t*)key, key_size);
    if (pos != 0 && pos < addr_store_.size()) {
        addr_store_[pos].clear();
    }
    pthread_rwlock_unlock(&rwlock_);
}

/**
 * @brief Delete a key (string version)
 */
void BinaryTreeDB::Delete(const string& key) {
    DeleteBuffer(key.c_str(), key.size());
}
