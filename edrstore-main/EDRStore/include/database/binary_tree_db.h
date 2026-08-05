/**
 * @file binary_tree_db.h
 * @brief AbsDatabase implementation backed by a full binary tree index
 *
 * Wraps BinaryTreeIndex to provide the AbsDatabase interface,
 * enabling drop-in replacement of hash-table-based InMemoryDatabase
 * with the paper's full binary tree storage for deduplication.
 *
 * Reference: CSP full binary tree for secure deduplication
 *
 * @version 0.1
 * @date 2023
 */

#ifndef EDRSTORE_BINARY_TREE_DB_H
#define EDRSTORE_BINARY_TREE_DB_H

#include "abs_db.h"
#include "binary_tree_index.h"
#include <pthread.h>

class BinaryTreeDB : public AbsDatabase {
    protected:
        string my_name_ = "BinaryTreeDB";

        // Core binary tree index (fp → leaf position)
        BinaryTreeIndex* tree_index_;

        // Parallel addr storage: leaf_position → address value string
        vector<string> addr_store_;

        // For serializing insert/query/delete — eliminates TOCTOU race between
        // tree_index_ hash map update and addr_store_ write
        pthread_rwlock_t rwlock_;
        pthread_rwlock_t addr_rwlock_;

    public:
        /**
         * @brief Construct with initial tree height
         * @param db_name database name/path (for compatibility)
         * @param initial_height initial tree height (default 10 → 1024 leaves)
         */
        BinaryTreeDB(string db_name, uint32_t initial_height = 10);

        /**
         * @brief Destroy and persist if needed
         */
        virtual ~BinaryTreeDB();

        /**
         * @brief open a database (load from file if exists)
         * @param db_name the db path
         * @return true success
         */
        bool OpenDB(string db_name);

        /**
         * @brief execute query over database
         * @param key key (fingerprint string)
         * @param value value (address, output)
         * @return true exist
         */
        bool Query(const string& key, string& value);

        /**
         * @brief insert the (key, value) pair
         * @param key key (fingerprint string)
         * @param value value (address string)
         * @return true
         */
        bool Insert(const string& key, const string& value);

        /**
         * @brief insert the (key, value) pair
         * @param key the key string
         * @param buf the value buffer
         * @param buf_size the buffer size
         * @return true
         */
        bool InsertBuffer(const string& key, const char* buf, size_t buf_size);

        /**
         * @brief insert the (key, value) pair
         * @param key the key buffer (fingerprint)
         * @param key_size the key size
         * @param buf the value buffer (address)
         * @param buf_size the buffer size
         * @return true if already existed
         */
        bool InsertBothBuffer(const char* key, size_t key_size, const char* buf,
            size_t buf_size);

        /**
         * @brief query the (key, value) pair
         * @param key the key buffer (fingerprint)
         * @param key_size the key size
         * @param value the value (address, output)
         * @return true exist
         */
        bool QueryBuffer(const char* key, size_t key_size, string& value);

        /**
         * @brief delete a given key
         * @param key key ptr
         * @param key_size key size
         */
        void DeleteBuffer(const char* key, size_t key_size);

        /**
         * @brief delete a given key
         * @param key key str
         */
        void Delete(const string& key);
};

#endif
