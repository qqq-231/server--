#include "../../include/security/secure_storage.h"

#include <openssl/crypto.h>
#include <openssl/rand.h>

#include <cstring>
#include <limits>
#include <unordered_map>

namespace edrstore::security {
namespace {

constexpr uint8_t kContainerMagic[8] =
    {'E', 'D', 'R', 'S', 'E', 'C', '0', '1'};
constexpr size_t kHeaderSize = 8 + 4 + 8 + kGcmNonceSize;
constexpr size_t kFormatOverhead = kHeaderSize + kGcmTagSize;
constexpr char kContainerKeyDomain[] =
    "EDRStore-container-AES256-GCM-v1";

bool ProtectOne(const std::filesystem::path& input,
    const std::filesystem::path& output, MasterKey& master,
    uint64_t& input_bytes, uint64_t& output_bytes, std::string& error) {
    std::vector<uint8_t> plaintext;
    if (!ReadFile(input, plaintext, error)) return false;
    input_bytes = plaintext.size();

    std::array<uint8_t, kSha256Size> key{};
    std::array<uint8_t, kGcmNonceSize> nonce{};
    std::array<uint8_t, kGcmTagSize> tag{};
    if (!master.DeriveSha256(kContainerKeyDomain,
            CanonicalFileID(input), key, error) ||
        RAND_bytes(nonce.data(), nonce.size()) != 1) {
        OPENSSL_cleanse(key.data(), key.size());
        if (error.empty()) error = "RAND_bytes failed for container nonce";
        return false;
    }

    std::array<uint8_t, kHeaderSize> header{};
    std::memcpy(header.data(), kContainerMagic, sizeof(kContainerMagic));
    PutLE32(header.data() + 8, kFormatVersion);
    PutLE64(header.data() + 12, plaintext.size());
    std::memcpy(header.data() + 20, nonce.data(), nonce.size());

    std::vector<uint8_t> ciphertext;
    const bool encrypted = EncryptGCM(key, nonce, header.data(), header.size(),
        plaintext.empty() ? nullptr : plaintext.data(), plaintext.size(),
        ciphertext, tag, error);
    OPENSSL_cleanse(key.data(), key.size());
    if (!plaintext.empty()) OPENSSL_cleanse(plaintext.data(), plaintext.size());
    if (!encrypted) return false;

    if (ciphertext.size() > std::numeric_limits<size_t>::max() -
            kFormatOverhead) {
        error = "secure container size overflow";
        return false;
    }
    std::vector<uint8_t> secure(kHeaderSize + ciphertext.size() + tag.size());
    std::memcpy(secure.data(), header.data(), header.size());
    if (!ciphertext.empty()) {
        std::memcpy(secure.data() + header.size(), ciphertext.data(),
            ciphertext.size());
        OPENSSL_cleanse(ciphertext.data(), ciphertext.size());
    }
    std::memcpy(secure.data() + header.size() + ciphertext.size(), tag.data(),
        tag.size());
    output_bytes = secure.size();
    const bool written = AtomicWriteNewFile(output, secure.data(), secure.size(),
        error);
    OPENSSL_cleanse(secure.data(), secure.size());
    OPENSSL_cleanse(tag.data(), tag.size());
    return written;
}

bool UnprotectOne(const std::filesystem::path& input,
    const std::filesystem::path& output, MasterKey& master,
    uint64_t& input_bytes, uint64_t& output_bytes, std::string& error) {
    std::vector<uint8_t> secure;
    if (!ReadFile(input, secure, error)) return false;
    input_bytes = secure.size();
    if (secure.size() < kFormatOverhead ||
        std::memcmp(secure.data(), kContainerMagic,
            sizeof(kContainerMagic)) != 0) {
        error = "invalid secure-container magic or truncated header";
        return false;
    }
    if (GetLE32(secure.data() + 8) != kFormatVersion) {
        error = "unsupported secure-container version";
        return false;
    }
    const uint64_t original_size = GetLE64(secure.data() + 12);
    if (original_size > std::numeric_limits<size_t>::max() ||
        original_size > std::numeric_limits<size_t>::max() - kFormatOverhead ||
        secure.size() != static_cast<size_t>(original_size) + kFormatOverhead) {
        error = "secure-container OriginalSize does not match file length";
        return false;
    }

    std::array<uint8_t, kGcmNonceSize> nonce{};
    std::memcpy(nonce.data(), secure.data() + 20, nonce.size());
    std::array<uint8_t, kSha256Size> key{};
    if (!master.DeriveSha256(kContainerKeyDomain,
            CanonicalFileID(input), key, error)) {
        return false;
    }
    const uint8_t* ciphertext = secure.data() + kHeaderSize;
    const uint8_t* tag = ciphertext + original_size;
    std::vector<uint8_t> plaintext;
    const bool decrypted = DecryptGCM(key, nonce, secure.data(), kHeaderSize,
        ciphertext, static_cast<size_t>(original_size), tag, plaintext, error);
    OPENSSL_cleanse(key.data(), key.size());
    if (!decrypted) return false;
    output_bytes = plaintext.size();
    const bool written = AtomicWriteNewFile(output,
        plaintext.empty() ? nullptr : plaintext.data(), plaintext.size(), error);
    if (!plaintext.empty()) OPENSSL_cleanse(plaintext.data(), plaintext.size());
    return written;
}

}  // namespace

bool SecureStorage::ProtectDirectory(const std::filesystem::path& input,
    const std::filesystem::path& output,
    const std::filesystem::path& master_key_path, DirectoryStats& stats,
    std::string& error) {
    stats = {};
    if (!PrepareSeparateOutputDirectory(input, output, error)) return false;
    std::vector<std::filesystem::path> files;
    if (!ListRegularFiles(input, files, error)) return false;
    MasterKey master;
    if (!master.LoadOrCreate(master_key_path, error)) return false;
    for (const auto& file : files) {
        uint64_t original_bytes = 0;
        uint64_t secure_bytes = 0;
        ++stats.total_files;
        if (!ProtectOne(file, output / file.filename(), master, original_bytes,
                secure_bytes, error)) {
            ++stats.failed_files;
            error = file.filename().string() + ": " + error;
            return false;
        }
        ++stats.matched_files;
        stats.original_total_bytes += original_bytes;
        stats.output_total_bytes += secure_bytes;
    }
    return true;
}

bool SecureStorage::UnprotectDirectory(const std::filesystem::path& input,
    const std::filesystem::path& output,
    const std::filesystem::path& master_key_path, DirectoryStats& stats,
    std::string& error) {
    stats = {};
    if (!PrepareSeparateOutputDirectory(input, output, error)) return false;
    std::vector<std::filesystem::path> files;
    if (!ListRegularFiles(input, files, error)) return false;
    MasterKey master;
    if (!master.LoadOrCreate(master_key_path, error)) return false;
    for (const auto& file : files) {
        uint64_t secure_bytes = 0;
        uint64_t recovered_bytes = 0;
        ++stats.total_files;
        if (!UnprotectOne(file, output / file.filename(), master, secure_bytes,
                recovered_bytes, error)) {
            ++stats.failed_files;
            error = file.filename().string() + ": " + error;
            return false;
        }
        ++stats.matched_files;
        stats.original_total_bytes += secure_bytes;
        stats.output_total_bytes += recovered_bytes;
    }
    return true;
}

bool SecureStorage::VerifyDirectories(const std::filesystem::path& original,
    const std::filesystem::path& recovered, DirectoryStats& stats,
    std::string& error) {
    stats = {};
    std::vector<std::filesystem::path> original_files;
    std::vector<std::filesystem::path> recovered_files;
    if (!ListRegularFiles(original, original_files, error) ||
        !ListRegularFiles(recovered, recovered_files, error)) {
        return false;
    }
    std::unordered_map<std::string, std::filesystem::path> recovered_by_name;
    for (const auto& file : recovered_files) {
        recovered_by_name.emplace(file.filename().string(), file);
        std::error_code fs_error;
        stats.output_total_bytes += std::filesystem::file_size(file, fs_error);
        if (fs_error) {
            error = "cannot read recovered file size: " + file.string();
            return false;
        }
    }
    stats.total_files = original_files.size();
    for (const auto& file : original_files) {
        std::array<uint8_t, kSha256Size> original_hash{};
        std::array<uint8_t, kSha256Size> recovered_hash{};
        uint64_t original_size = 0;
        uint64_t recovered_size = 0;
        if (!SHA256File(file, original_hash, original_size, error)) return false;
        stats.original_total_bytes += original_size;
        const auto found = recovered_by_name.find(file.filename().string());
        if (found == recovered_by_name.end() ||
            !SHA256File(found->second, recovered_hash, recovered_size, error) ||
            original_size != recovered_size ||
            CRYPTO_memcmp(original_hash.data(), recovered_hash.data(),
                original_hash.size()) != 0) {
            ++stats.failed_files;
        } else {
            ++stats.matched_files;
        }
        if (found != recovered_by_name.end()) {
            recovered_by_name.erase(found);
        }
    }
    stats.failed_files += recovered_by_name.size();
    if (!recovered_by_name.empty()) {
        stats.total_files += recovered_by_name.size();
    }
    if (stats.failed_files != 0) {
        error = "container SHA-256 verification found mismatched, missing, or extra files";
        return false;
    }
    return true;
}

}  // namespace edrstore::security
