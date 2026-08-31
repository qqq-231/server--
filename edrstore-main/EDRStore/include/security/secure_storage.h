#ifndef EDRSTORE_SECURE_STORAGE_H
#define EDRSTORE_SECURE_STORAGE_H

#include "secure_common.h"

namespace edrstore::security {

struct DirectoryStats {
    uint64_t total_files = 0;
    uint64_t matched_files = 0;
    uint64_t failed_files = 0;
    uint64_t original_total_bytes = 0;
    uint64_t output_total_bytes = 0;
};

class SecureStorage {
  public:
    bool ProtectDirectory(const std::filesystem::path& input,
        const std::filesystem::path& output,
        const std::filesystem::path& master_key_path,
        DirectoryStats& stats, std::string& error);
    bool UnprotectDirectory(const std::filesystem::path& input,
        const std::filesystem::path& output,
        const std::filesystem::path& master_key_path,
        DirectoryStats& stats, std::string& error);
    bool VerifyDirectories(const std::filesystem::path& original,
        const std::filesystem::path& recovered,
        DirectoryStats& stats, std::string& error);
};

}  // namespace edrstore::security

#endif
