#include "../../include/security/secure_common.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>

namespace edrstore::security {
namespace {

bool WriteExact(int fd, const uint8_t* data, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        const ssize_t written = write(fd, data + offset, size - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return false;
        offset += static_cast<size_t>(written);
    }
    return true;
}

void SyncDirectory(const std::filesystem::path& path) {
    const std::filesystem::path parent = path.parent_path().empty() ? "." :
        path.parent_path();
    const int fd = open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd >= 0) {
        fsync(fd);
        close(fd);
    }
}

bool ReadExactKeyFile(const std::filesystem::path& path,
    std::array<uint8_t, kMasterKeySize>& key, std::string& error) {
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        error = "cannot open master key: " + path.string();
        return false;
    }
    struct stat status {};
    if (fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) ||
        status.st_size != static_cast<off_t>(key.size())) {
        close(fd);
        error = "master key must be a regular 32-byte file";
        return false;
    }
    size_t offset = 0;
    while (offset < key.size()) {
        const ssize_t read_size = read(fd, key.data() + offset,
            key.size() - offset);
        if (read_size < 0 && errno == EINTR) continue;
        if (read_size <= 0) {
            close(fd);
            error = "cannot read complete master key";
            return false;
        }
        offset += static_cast<size_t>(read_size);
    }
    if (fchmod(fd, S_IRUSR | S_IWUSR) != 0) {
        close(fd);
        error = "cannot enforce master-key mode 0600";
        return false;
    }
    close(fd);
    return true;
}

bool UpdateFileDigest(std::ifstream& input, EVP_MD_CTX* context,
    uint64_t& file_size, std::string& error, bool sign) {
    std::array<char, 64 * 1024> buffer{};
    file_size = 0;
    while (input.good()) {
        input.read(buffer.data(), buffer.size());
        const std::streamsize count = input.gcount();
        if (count > 0) {
            if (file_size > std::numeric_limits<uint64_t>::max() -
                    static_cast<uint64_t>(count)) {
                error = "input file is too large";
                return false;
            }
            file_size += static_cast<uint64_t>(count);
            const int ok = sign ?
                EVP_DigestSignUpdate(context, buffer.data(), count) :
                EVP_DigestUpdate(context, buffer.data(), count);
            if (ok != 1) {
                error = "OpenSSL digest update failed";
                return false;
            }
        }
    }
    if (!input.eof()) {
        error = "input file read failed";
        return false;
    }
    return true;
}

}  // namespace

MasterKey::MasterKey() = default;

MasterKey::~MasterKey() {
    OPENSSL_cleanse(key_.data(), key_.size());
    if (locked_) {
        munlock(key_.data(), key_.size());
    }
}

void MasterKey::TryLockMemory() {
    if (locked_) return;
    if (mlock(key_.data(), key_.size()) == 0) {
        locked_ = true;
        return;
    }
    std::fprintf(stderr,
        "SECURITY WARNING: mlock failed for the 32-byte master-key buffer: %s\n",
        std::strerror(errno));
}

bool MasterKey::LoadOrCreate(const std::filesystem::path& path,
    std::string& error) {
    if (initialized_) return true;
    std::error_code fs_error;
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, fs_error);
        if (fs_error || chmod(parent.c_str(), S_IRWXU) != 0) {
            error = "cannot create protected key directory: " +
                parent.string();
            return false;
        }
    }

    if (std::filesystem::exists(path, fs_error)) {
        if (fs_error || !ReadExactKeyFile(path, key_, error)) return false;
        initialized_ = true;
        TryLockMemory();
        return true;
    }
    if (fs_error) {
        error = "cannot inspect master-key path";
        return false;
    }

    std::array<uint8_t, kMasterKeySize> generated{};
    if (RAND_bytes(generated.data(), generated.size()) != 1) {
        error = "RAND_bytes failed while generating master key";
        return false;
    }
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL |
        O_CLOEXEC | O_NOFOLLOW, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        const int open_error = errno;
        OPENSSL_cleanse(generated.data(), generated.size());
        if (open_error == EEXIST && ReadExactKeyFile(path, key_, error)) {
            initialized_ = true;
            TryLockMemory();
            return true;
        }
        error = "cannot create master key: " + path.string();
        return false;
    }
    bool ok = fchmod(fd, S_IRUSR | S_IWUSR) == 0 &&
        WriteExact(fd, generated.data(), generated.size()) && fsync(fd) == 0;
    if (close(fd) != 0) ok = false;
    if (!ok) {
        unlink(path.c_str());
        OPENSSL_cleanse(generated.data(), generated.size());
        error = "cannot persist master key";
        return false;
    }
    key_ = generated;
    OPENSSL_cleanse(generated.data(), generated.size());
    SyncDirectory(path);
    initialized_ = true;
    TryLockMemory();
    return true;
}

bool MasterKey::DeriveSha256(const std::string& domain,
    const std::string& context,
    std::array<uint8_t, kSha256Size>& output, std::string& error) const {
    if (!initialized_ || domain.empty()) {
        error = "master key is not initialized or KDF domain is empty";
        return false;
    }
    std::vector<uint8_t> input(domain.size() + context.size());
    std::memcpy(input.data(), domain.data(), domain.size());
    if (!context.empty()) {
        std::memcpy(input.data() + domain.size(), context.data(),
            context.size());
    }
    unsigned int output_size = 0;
    const bool ok = HMAC(EVP_sha256(), key_.data(), key_.size(), input.data(),
        input.size(), output.data(), &output_size) != nullptr &&
        output_size == output.size();
    OPENSSL_cleanse(input.data(), input.size());
    if (!ok) error = "HMAC-SHA256 key derivation failed";
    return ok;
}

bool ReadFile(const std::filesystem::path& path, std::vector<uint8_t>& data,
    std::string& error) {
    std::error_code fs_error;
    const auto status = std::filesystem::symlink_status(path, fs_error);
    if (fs_error || !std::filesystem::is_regular_file(status)) {
        error = "input is not a regular non-symlink file: " + path.string();
        return false;
    }
    const uintmax_t size = std::filesystem::file_size(path, fs_error);
    if (fs_error || size > std::numeric_limits<size_t>::max()) {
        error = "cannot determine safe input file size: " + path.string();
        return false;
    }
    data.resize(static_cast<size_t>(size));
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        error = "cannot open input file: " + path.string();
        return false;
    }
    if (!data.empty()) {
        input.read(reinterpret_cast<char*>(data.data()), data.size());
        if (input.gcount() != static_cast<std::streamsize>(data.size())) {
            error = "short read from input file: " + path.string();
            return false;
        }
    }
    return true;
}

bool AtomicWriteNewFile(const std::filesystem::path& path,
    const uint8_t* data, size_t size, std::string& error) {
    if (size != 0 && data == nullptr) {
        error = "invalid null output buffer";
        return false;
    }
    std::error_code fs_error;
    if (std::filesystem::exists(path, fs_error) || fs_error) {
        error = "refusing to overwrite existing output: " + path.string();
        return false;
    }
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, fs_error);
        if (fs_error) {
            error = "cannot create output directory: " + parent.string();
            return false;
        }
    }
    std::string temporary_template = path.string() + ".tmp.XXXXXX";
    std::vector<char> temporary_name(temporary_template.begin(),
        temporary_template.end());
    temporary_name.push_back('\0');
    const int fd = mkstemp(temporary_name.data());
    if (fd < 0) {
        error = "cannot create output temporary file";
        return false;
    }
    bool ok = fchmod(fd, S_IRUSR | S_IWUSR) == 0 &&
        WriteExact(fd, data, size) && fsync(fd) == 0;
    if (close(fd) != 0) ok = false;
    if (ok) {
        // link() provides an atomic no-replace publication in the same
        // directory. The private temporary name is removed afterwards.
        ok = link(temporary_name.data(), path.c_str()) == 0;
    }
    unlink(temporary_name.data());
    if (!ok) {
        error = "cannot atomically publish output without overwriting: " +
            path.string();
        return false;
    }
    SyncDirectory(path);
    return true;
}

bool PrepareSeparateOutputDirectory(const std::filesystem::path& input,
    const std::filesystem::path& output, std::string& error) {
    std::error_code fs_error;
    if (!std::filesystem::is_directory(input, fs_error) || fs_error) {
        error = "input directory does not exist: " + input.string();
        return false;
    }
    const auto input_path = std::filesystem::weakly_canonical(input, fs_error);
    if (fs_error) {
        error = "cannot canonicalize input directory";
        return false;
    }
    fs_error.clear();
    const auto output_path = std::filesystem::weakly_canonical(output, fs_error);
    if (!fs_error && input_path == output_path) {
        error = "input and output directories must be different";
        return false;
    }
    fs_error.clear();
    std::filesystem::create_directories(output, fs_error);
    if (fs_error || !std::filesystem::is_directory(output)) {
        error = "cannot create output directory: " + output.string();
        return false;
    }
    return true;
}

bool ListRegularFiles(const std::filesystem::path& directory,
    std::vector<std::filesystem::path>& files, std::string& error) {
    files.clear();
    std::error_code fs_error;
    for (std::filesystem::directory_iterator iterator(directory, fs_error), end;
         !fs_error && iterator != end; iterator.increment(fs_error)) {
        const auto status = iterator->symlink_status(fs_error);
        if (fs_error) break;
        if (std::filesystem::is_regular_file(status)) {
            files.push_back(iterator->path());
        }
    }
    if (fs_error) {
        error = "cannot enumerate directory: " + directory.string();
        return false;
    }
    std::sort(files.begin(), files.end());
    return true;
}

std::string CanonicalFileID(const std::filesystem::path& path) {
    return path.filename().string();
}

void PutLE32(uint8_t* output, uint32_t value) {
    for (size_t index = 0; index < 4; ++index) {
        output[index] = static_cast<uint8_t>(value >> (index * 8));
    }
}

void PutLE64(uint8_t* output, uint64_t value) {
    for (size_t index = 0; index < 8; ++index) {
        output[index] = static_cast<uint8_t>(value >> (index * 8));
    }
}

uint32_t GetLE32(const uint8_t* input) {
    uint32_t value = 0;
    for (size_t index = 0; index < 4; ++index) {
        value |= static_cast<uint32_t>(input[index]) << (index * 8);
    }
    return value;
}

uint64_t GetLE64(const uint8_t* input) {
    uint64_t value = 0;
    for (size_t index = 0; index < 8; ++index) {
        value |= static_cast<uint64_t>(input[index]) << (index * 8);
    }
    return value;
}

bool EncryptGCM(const std::array<uint8_t, kSha256Size>& key,
    const std::array<uint8_t, kGcmNonceSize>& nonce, const uint8_t* aad,
    size_t aad_size, const uint8_t* plaintext, size_t plaintext_size,
    std::vector<uint8_t>& ciphertext,
    std::array<uint8_t, kGcmTagSize>& tag, std::string& error) {
    if (plaintext_size > static_cast<size_t>(INT_MAX) ||
        aad_size > static_cast<size_t>(INT_MAX)) {
        error = "GCM input exceeds supported size";
        return false;
    }
    ciphertext.resize(plaintext_size);
    EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
    uint8_t final_buffer[16] = {0};
    int count = 0;
    int total = 0;
    bool ok = context != nullptr &&
        EVP_EncryptInit_ex(context, EVP_aes_256_gcm(), nullptr, nullptr,
            nullptr) == 1 &&
        EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN, nonce.size(),
            nullptr) == 1 &&
        EVP_EncryptInit_ex(context, nullptr, nullptr, key.data(),
            nonce.data()) == 1;
    if (ok && aad_size != 0) {
        ok = EVP_EncryptUpdate(context, nullptr, &count, aad,
            static_cast<int>(aad_size)) == 1;
    }
    if (ok && plaintext_size != 0) {
        ok = EVP_EncryptUpdate(context, ciphertext.data(), &count, plaintext,
            static_cast<int>(plaintext_size)) == 1;
        total = count;
    }
    if (ok) {
        uint8_t* final_output = ciphertext.empty() ? final_buffer :
            ciphertext.data() + total;
        ok = EVP_EncryptFinal_ex(context, final_output, &count) == 1;
        total += count;
    }
    if (ok) {
        ok = static_cast<size_t>(total) == plaintext_size &&
            EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_GET_TAG, tag.size(),
                tag.data()) == 1;
    }
    EVP_CIPHER_CTX_free(context);
    OPENSSL_cleanse(final_buffer, sizeof(final_buffer));
    if (!ok) {
        if (!ciphertext.empty()) {
            OPENSSL_cleanse(ciphertext.data(), ciphertext.size());
        }
        ciphertext.clear();
        OPENSSL_cleanse(tag.data(), tag.size());
        error = "AES-256-GCM encryption failed";
    }
    return ok;
}

bool DecryptGCM(const std::array<uint8_t, kSha256Size>& key,
    const std::array<uint8_t, kGcmNonceSize>& nonce, const uint8_t* aad,
    size_t aad_size, const uint8_t* ciphertext, size_t ciphertext_size,
    const uint8_t tag[kGcmTagSize], std::vector<uint8_t>& plaintext,
    std::string& error) {
    if (ciphertext_size > static_cast<size_t>(INT_MAX) ||
        aad_size > static_cast<size_t>(INT_MAX)) {
        error = "GCM input exceeds supported size";
        return false;
    }
    plaintext.resize(ciphertext_size);
    EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
    uint8_t final_buffer[16] = {0};
    int count = 0;
    int total = 0;
    bool ok = context != nullptr &&
        EVP_DecryptInit_ex(context, EVP_aes_256_gcm(), nullptr, nullptr,
            nullptr) == 1 &&
        EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN, nonce.size(),
            nullptr) == 1 &&
        EVP_DecryptInit_ex(context, nullptr, nullptr, key.data(),
            nonce.data()) == 1;
    if (ok && aad_size != 0) {
        ok = EVP_DecryptUpdate(context, nullptr, &count, aad,
            static_cast<int>(aad_size)) == 1;
    }
    if (ok && ciphertext_size != 0) {
        ok = EVP_DecryptUpdate(context, plaintext.data(), &count, ciphertext,
            static_cast<int>(ciphertext_size)) == 1;
        total = count;
    }
    if (ok) {
        ok = EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_TAG, kGcmTagSize,
            const_cast<uint8_t*>(tag)) == 1;
    }
    if (ok) {
        uint8_t* final_output = plaintext.empty() ? final_buffer :
            plaintext.data() + total;
        ok = EVP_DecryptFinal_ex(context, final_output, &count) == 1;
        total += count;
    }
    ok = ok && static_cast<size_t>(total) == ciphertext_size;
    EVP_CIPHER_CTX_free(context);
    OPENSSL_cleanse(final_buffer, sizeof(final_buffer));
    if (!ok) {
        if (!plaintext.empty()) {
            OPENSSL_cleanse(plaintext.data(), plaintext.size());
        }
        plaintext.clear();
        error = "AES-256-GCM authentication failed";
    }
    return ok;
}

bool SHA256File(const std::filesystem::path& path,
    std::array<uint8_t, kSha256Size>& digest, uint64_t& file_size,
    std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        error = "cannot open file for SHA-256: " + path.string();
        return false;
    }
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    bool ok = context != nullptr &&
        EVP_DigestInit_ex(context, EVP_sha256(), nullptr) == 1 &&
        UpdateFileDigest(input, context, file_size, error, false);
    unsigned int digest_size = 0;
    if (ok) {
        ok = EVP_DigestFinal_ex(context, digest.data(), &digest_size) == 1 &&
            digest_size == digest.size();
    }
    EVP_MD_CTX_free(context);
    if (!ok && error.empty()) error = "SHA-256 calculation failed";
    return ok;
}

bool HMACSHA256File(const std::filesystem::path& path,
    const std::array<uint8_t, kSha256Size>& key,
    std::array<uint8_t, kSha256Size>& digest, uint64_t& file_size,
    std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        error = "cannot open file for HMAC: " + path.string();
        return false;
    }
    EVP_PKEY* pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_HMAC, nullptr,
        key.data(), key.size());
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    bool ok = pkey != nullptr && context != nullptr &&
        EVP_DigestSignInit(context, nullptr, EVP_sha256(), nullptr, pkey) == 1 &&
        UpdateFileDigest(input, context, file_size, error, true);
    size_t digest_size = digest.size();
    if (ok) {
        ok = EVP_DigestSignFinal(context, digest.data(), &digest_size) == 1 &&
            digest_size == digest.size();
    }
    EVP_MD_CTX_free(context);
    EVP_PKEY_free(pkey);
    if (!ok && error.empty()) error = "HMAC-SHA256 calculation failed";
    return ok;
}

}  // namespace edrstore::security
