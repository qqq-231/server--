#include "../../include/security/km_master_key.h"

#include <openssl/crypto.h>
#include <openssl/rand.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace edrstore::security {
namespace {

constexpr std::array<uint8_t, 8> kMagic = {
    'E', 'D', 'R', 'K', 'M', '0', '0', '1'};
constexpr std::array<uint8_t, 4> kVersion = {0, 0, 0, 1};
constexpr size_t kFileSize = kMagic.size() + kVersion.size() +
    KMMasterKey::kSecretSize * 2;

bool WriteExact(int fd, const uint8_t* data, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        const ssize_t count = write(fd, data + offset, size - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        offset += static_cast<size_t>(count);
    }
    return true;
}

bool ReadExact(int fd, uint8_t* data, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        const ssize_t count = read(fd, data + offset, size - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        offset += static_cast<size_t>(count);
    }
    return true;
}

bool ValidateKeyDirectory(const std::filesystem::path& directory,
    std::string& error) {
    struct stat status {};
    if (lstat(directory.c_str(), &status) != 0 || !S_ISDIR(status.st_mode) ||
        S_ISLNK(status.st_mode)) {
        error = "credential directory is missing or is not a real directory";
        return false;
    }
    if (status.st_uid != geteuid()) {
        error = "credential directory owner does not match the process user";
        return false;
    }
    if ((status.st_mode & 0777) != 0700) {
        error = "credential directory permission must be exactly 0700";
        return false;
    }
    return true;
}

void SyncDirectory(const std::filesystem::path& directory) {
    const int fd = open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd >= 0) {
        fsync(fd);
        close(fd);
    }
}

}  // namespace

KMMasterKey::KMMasterKey()
    : global_secret_{0}, padding_secret_{0}, loaded_(false),
      global_locked_(false), padding_locked_(false) {}

KMMasterKey::~KMMasterKey() {
    ClearSecrets();
}

bool KMMasterKey::LockSecrets() {
    if (!global_locked_) {
        if (mlock(global_secret_, sizeof(global_secret_)) == 0) {
            global_locked_ = true;
        } else {
            std::fprintf(stderr,
                "SECURITY WARNING: unable to lock KM credential buffer A: %s\n",
                std::strerror(errno));
        }
    }
    if (!padding_locked_) {
        if (mlock(padding_secret_, sizeof(padding_secret_)) == 0) {
            padding_locked_ = true;
        } else {
            std::fprintf(stderr,
                "SECURITY WARNING: unable to lock KM credential buffer B: %s\n",
                std::strerror(errno));
        }
    }
    return global_locked_ && padding_locked_;
}

void KMMasterKey::ClearSecrets() {
    OPENSSL_cleanse(global_secret_, sizeof(global_secret_));
    OPENSSL_cleanse(padding_secret_, sizeof(padding_secret_));
    if (global_locked_) munlock(global_secret_, sizeof(global_secret_));
    if (padding_locked_) munlock(padding_secret_, sizeof(padding_secret_));
    global_locked_ = false;
    padding_locked_ = false;
    loaded_ = false;
}

bool KMMasterKey::CreateNew(const std::string& path, std::string& error) {
    if (loaded_) {
        error = "credential object is already initialized";
        return false;
    }
    const std::filesystem::path key_path(path);
    const std::filesystem::path directory = key_path.parent_path();
    if (directory.empty()) {
        error = "credential path must include a protected directory";
        return false;
    }

    std::error_code fs_error;
    std::filesystem::create_directories(directory, fs_error);
    if (fs_error || chmod(directory.c_str(), 0700) != 0 ||
        !ValidateKeyDirectory(directory, error)) {
        if (error.empty()) error = "cannot create protected credential directory";
        return false;
    }
    if (RAND_bytes(global_secret_, sizeof(global_secret_)) != 1 ||
        RAND_bytes(padding_secret_, sizeof(padding_secret_)) != 1) {
        ClearSecrets();
        error = "cryptographic random generation failed";
        return false;
    }

    const int fd = open(key_path.c_str(), O_WRONLY | O_CREAT | O_EXCL |
        O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) {
        ClearSecrets();
        error = errno == EEXIST ?
            "credential file already exists; refusing to overwrite it" :
            "cannot create credential file";
        return false;
    }
    bool ok = fchmod(fd, 0600) == 0 &&
        WriteExact(fd, kMagic.data(), kMagic.size()) &&
        WriteExact(fd, kVersion.data(), kVersion.size()) &&
        WriteExact(fd, global_secret_, sizeof(global_secret_)) &&
        WriteExact(fd, padding_secret_, sizeof(padding_secret_)) &&
        fsync(fd) == 0;
    if (close(fd) != 0) ok = false;
    if (!ok) {
        unlink(key_path.c_str());
        ClearSecrets();
        error = "cannot persist complete credential file";
        return false;
    }
    SyncDirectory(directory);
    loaded_ = true;
    LockSecrets();
    return true;
}

bool KMMasterKey::LoadExisting(const std::string& path, std::string& error) {
    if (loaded_) {
        error = "credential object is already initialized";
        return false;
    }
    const std::filesystem::path key_path(path);
    if (!ValidateKeyDirectory(key_path.parent_path(), error)) return false;

    const int fd = open(key_path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        error = "credential file is missing or cannot be opened";
        return false;
    }
    struct stat status {};
    if (fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) ||
        status.st_uid != geteuid() || (status.st_mode & 0777) != 0600 ||
        status.st_size != static_cast<off_t>(kFileSize)) {
        close(fd);
        error = "credential file must be owner-only 0600, regular, and version-sized";
        return false;
    }

    std::array<uint8_t, kMagic.size()> magic{};
    std::array<uint8_t, kVersion.size()> version{};
    const bool ok = ReadExact(fd, magic.data(), magic.size()) &&
        ReadExact(fd, version.data(), version.size()) &&
        ReadExact(fd, global_secret_, sizeof(global_secret_)) &&
        ReadExact(fd, padding_secret_, sizeof(padding_secret_));
    const int close_status = close(fd);
    if (!ok || close_status != 0 || magic != kMagic || version != kVersion) {
        ClearSecrets();
        error = "credential file is incomplete or has an unsupported format";
        return false;
    }
    loaded_ = true;
    LockSecrets();
    return true;
}

const uint8_t* KMMasterKey::GlobalSecret() const {
    return loaded_ ? global_secret_ : nullptr;
}

const uint8_t* KMMasterKey::PaddingSecret() const {
    return loaded_ ? padding_secret_ : nullptr;
}

bool KMMasterKey::IsLoaded() const {
    return loaded_;
}

}  // namespace edrstore::security
