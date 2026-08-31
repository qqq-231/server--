#include "../../include/security/file_integrity.h"

#include <openssl/crypto.h>

#include <cstring>

namespace edrstore::security {
namespace {

constexpr uint8_t kFileMacMagic[8] =
    {'E', 'D', 'R', 'M', 'A', 'C', '0', '1'};
constexpr size_t kMetadataSize = 8 + 4 + 8 + kSha256Size;
constexpr char kFileKeyDomain[] = "EDRStore-file-HMAC-v1";

bool DeriveFileKey(MasterKey& master,
    std::array<uint8_t, kSha256Size>& key, std::string& error) {
    return master.DeriveSha256(kFileKeyDomain, "", key, error);
}

}  // namespace

bool FileIntegrity::Create(const std::filesystem::path& input,
    const std::filesystem::path& metadata,
    const std::filesystem::path& master_key_path, uint64_t& file_size,
    std::string& error) {
    MasterKey master;
    std::array<uint8_t, kSha256Size> key{};
    std::array<uint8_t, kSha256Size> mac{};
    if (!master.LoadOrCreate(master_key_path, error) ||
        !DeriveFileKey(master, key, error) ||
        !HMACSHA256File(input, key, mac, file_size, error)) {
        OPENSSL_cleanse(key.data(), key.size());
        return false;
    }
    OPENSSL_cleanse(key.data(), key.size());
    std::array<uint8_t, kMetadataSize> record{};
    std::memcpy(record.data(), kFileMacMagic, sizeof(kFileMacMagic));
    PutLE32(record.data() + 8, kFormatVersion);
    PutLE64(record.data() + 12, file_size);
    std::memcpy(record.data() + 20, mac.data(), mac.size());
    const bool written = AtomicWriteNewFile(metadata, record.data(),
        record.size(), error);
    OPENSSL_cleanse(mac.data(), mac.size());
    OPENSSL_cleanse(record.data(), record.size());
    return written;
}

bool FileIntegrity::Verify(const std::filesystem::path& input,
    const std::filesystem::path& metadata,
    const std::filesystem::path& master_key_path, uint64_t& file_size,
    std::string& error) {
    std::vector<uint8_t> record;
    if (!ReadFile(metadata, record, error)) return false;
    if (record.size() != kMetadataSize ||
        std::memcmp(record.data(), kFileMacMagic, sizeof(kFileMacMagic)) != 0 ||
        GetLE32(record.data() + 8) != kFormatVersion) {
        error = "invalid file-integrity metadata format";
        return false;
    }
    const uint64_t expected_size = GetLE64(record.data() + 12);
    MasterKey master;
    std::array<uint8_t, kSha256Size> key{};
    std::array<uint8_t, kSha256Size> computed{};
    if (!master.LoadOrCreate(master_key_path, error) ||
        !DeriveFileKey(master, key, error) ||
        !HMACSHA256File(input, key, computed, file_size, error)) {
        OPENSSL_cleanse(key.data(), key.size());
        return false;
    }
    OPENSSL_cleanse(key.data(), key.size());
    const bool matches = file_size == expected_size &&
        CRYPTO_memcmp(computed.data(), record.data() + 20,
            computed.size()) == 0;
    OPENSSL_cleanse(computed.data(), computed.size());
    if (!matches) {
        error = "File integrity verification FAILED";
        return false;
    }
    return true;
}

}  // namespace edrstore::security
