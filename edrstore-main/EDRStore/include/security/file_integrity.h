#ifndef EDRSTORE_FILE_INTEGRITY_H
#define EDRSTORE_FILE_INTEGRITY_H

#include "secure_common.h"

namespace edrstore::security {

class FileIntegrity {
  public:
    bool Create(const std::filesystem::path& input,
        const std::filesystem::path& metadata,
        const std::filesystem::path& master_key_path,
        uint64_t& file_size, std::string& error);
    bool Verify(const std::filesystem::path& input,
        const std::filesystem::path& metadata,
        const std::filesystem::path& master_key_path,
        uint64_t& file_size, std::string& error);
};

}  // namespace edrstore::security

#endif
