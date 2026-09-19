/**
 * @file absDatabase.cc
 * @author Zuoru YANG (zryang@cse.cuhk.edu.hk)
 * @brief implement the interface defined in abs database.h
 * @version 0.1
 * @date 2020-01-25
 * 
 * @copyright Copyright (c) 2020
 * 
 */

#include "../../include/database/abs_db.h"

/**
 * @brief Construct a new Abs Database object
 * 
 */
AbsDatabase::AbsDatabase() {

}

vector<bool> AbsDatabase::BatchQueryBuffer(const vector<string>& keys,
    vector<string>& values) {
    values.assign(keys.size(), string());
    vector<bool> found(keys.size(), false);
    for (size_t key_idx = 0; key_idx < keys.size(); key_idx++) {
        found[key_idx] = this->QueryBuffer(keys[key_idx].data(),
            keys[key_idx].size(), values[key_idx]);
    }
    return found;
}
