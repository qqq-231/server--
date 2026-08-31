#ifndef EDRSTORE_RECIPE_SECURITY_H
#define EDRSTORE_RECIPE_SECURITY_H

#include "secure_common.h"

namespace edrstore::security {

class RecipeSecurity {
  public:
    bool ProtectFile(const std::filesystem::path& input,
        const std::filesystem::path& output,
        const std::filesystem::path& master_key_path,
        uint64_t& input_bytes, uint64_t& output_bytes, std::string& error);
    bool UnprotectFile(const std::filesystem::path& input,
        const std::filesystem::path& output,
        const std::filesystem::path& master_key_path,
        uint64_t& input_bytes, uint64_t& output_bytes, std::string& error);
};

}  // namespace edrstore::security

#endif
