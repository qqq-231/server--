#ifndef EDRSTORE_SECURE_COMMON_H
#define EDRSTORE_SECURE_COMMON_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace edrstore::security {

constexpr size_t kMasterKeySize = 32;
constexpr size_t kGcmNonceSize = 12;
constexpr size_t kGcmTagSize = 16;
constexpr size_t kSha256Size = 32;
constexpr uint32_t kFormatVersion = 1;

class MasterKey {
  public:
    MasterKey();
    ~MasterKey();
    MasterKey(const MasterKey&) = delete;
    MasterKey& operator=(const MasterKey&) = delete;

    bool LoadOrCreate(const std::filesystem::path& path, std::string& error);
    bool DeriveSha256(const std::string& domain, const std::string& context,
        std::array<uint8_t, kSha256Size>& output, std::string& error) const;

  private:
    void TryLockMemory();
    std::array<uint8_t, kMasterKeySize> key_{};
    bool initialized_ = false;
    bool locked_ = false;
};

bool ReadFile(const std::filesystem::path& path, std::vector<uint8_t>& data,
    std::string& error);
bool AtomicWriteNewFile(const std::filesystem::path& path,
    const uint8_t* data, size_t size, std::string& error);
bool PrepareSeparateOutputDirectory(const std::filesystem::path& input,
    const std::filesystem::path& output, std::string& error);
bool ListRegularFiles(const std::filesystem::path& directory,
    std::vector<std::filesystem::path>& files, std::string& error);
std::string CanonicalFileID(const std::filesystem::path& path);

void PutLE32(uint8_t* output, uint32_t value);
void PutLE64(uint8_t* output, uint64_t value);
uint32_t GetLE32(const uint8_t* input);
uint64_t GetLE64(const uint8_t* input);

bool EncryptGCM(const std::array<uint8_t, kSha256Size>& key,
    const std::array<uint8_t, kGcmNonceSize>& nonce,
    const uint8_t* aad, size_t aad_size, const uint8_t* plaintext,
    size_t plaintext_size, std::vector<uint8_t>& ciphertext,
    std::array<uint8_t, kGcmTagSize>& tag, std::string& error);
bool DecryptGCM(const std::array<uint8_t, kSha256Size>& key,
    const std::array<uint8_t, kGcmNonceSize>& nonce,
    const uint8_t* aad, size_t aad_size, const uint8_t* ciphertext,
    size_t ciphertext_size, const uint8_t tag[kGcmTagSize],
    std::vector<uint8_t>& plaintext, std::string& error);

bool SHA256File(const std::filesystem::path& path,
    std::array<uint8_t, kSha256Size>& digest, uint64_t& file_size,
    std::string& error);
bool HMACSHA256File(const std::filesystem::path& path,
    const std::array<uint8_t, kSha256Size>& key,
    std::array<uint8_t, kSha256Size>& digest, uint64_t& file_size,
    std::string& error);

}  // namespace edrstore::security

#endif
