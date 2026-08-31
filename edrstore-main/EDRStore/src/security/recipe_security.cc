#include "../../include/security/recipe_security.h"

#include <openssl/crypto.h>
#include <openssl/rand.h>

#include <cstring>
#include <limits>

namespace edrstore::security {
namespace {

constexpr uint8_t kRecipeMagic[8] =
    {'E', 'D', 'R', 'K', 'E', 'Y', '0', '1'};
constexpr size_t kHeaderSize = 8 + 4 + 8 + kGcmNonceSize;
constexpr size_t kFormatOverhead = kHeaderSize + kGcmTagSize;
constexpr char kRecipeKeyDomain[] = "EDRStore-recipe-AES256-GCM-v1";

bool DeriveRecipeKey(MasterKey& master,
    std::array<uint8_t, kSha256Size>& key, std::string& error) {
    return master.DeriveSha256(kRecipeKeyDomain, "", key, error);
}

}  // namespace

bool RecipeSecurity::ProtectFile(const std::filesystem::path& input,
    const std::filesystem::path& output,
    const std::filesystem::path& master_key_path, uint64_t& input_bytes,
    uint64_t& output_bytes, std::string& error) {
    input_bytes = 0;
    output_bytes = 0;
    std::vector<uint8_t> plaintext;
    if (!ReadFile(input, plaintext, error)) return false;
    input_bytes = plaintext.size();

    MasterKey master;
    std::array<uint8_t, kSha256Size> key{};
    std::array<uint8_t, kGcmNonceSize> nonce{};
    std::array<uint8_t, kGcmTagSize> tag{};
    if (!master.LoadOrCreate(master_key_path, error) ||
        !DeriveRecipeKey(master, key, error) ||
        RAND_bytes(nonce.data(), nonce.size()) != 1) {
        OPENSSL_cleanse(key.data(), key.size());
        if (error.empty()) error = "RAND_bytes failed for recipe nonce";
        return false;
    }

    std::array<uint8_t, kHeaderSize> header{};
    std::memcpy(header.data(), kRecipeMagic, sizeof(kRecipeMagic));
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

bool RecipeSecurity::UnprotectFile(const std::filesystem::path& input,
    const std::filesystem::path& output,
    const std::filesystem::path& master_key_path, uint64_t& input_bytes,
    uint64_t& output_bytes, std::string& error) {
    input_bytes = 0;
    output_bytes = 0;
    std::vector<uint8_t> secure;
    if (!ReadFile(input, secure, error)) return false;
    input_bytes = secure.size();
    if (secure.size() < kFormatOverhead ||
        std::memcmp(secure.data(), kRecipeMagic, sizeof(kRecipeMagic)) != 0) {
        error = "invalid secure-recipe magic or truncated header";
        return false;
    }
    if (GetLE32(secure.data() + 8) != kFormatVersion) {
        error = "unsupported secure-recipe version";
        return false;
    }
    const uint64_t original_size = GetLE64(secure.data() + 12);
    if (original_size > std::numeric_limits<size_t>::max() ||
        original_size > std::numeric_limits<size_t>::max() - kFormatOverhead ||
        secure.size() != static_cast<size_t>(original_size) + kFormatOverhead) {
        error = "secure-recipe OriginalSize does not match file length";
        return false;
    }

    MasterKey master;
    std::array<uint8_t, kSha256Size> key{};
    std::array<uint8_t, kGcmNonceSize> nonce{};
    if (!master.LoadOrCreate(master_key_path, error) ||
        !DeriveRecipeKey(master, key, error)) {
        return false;
    }
    std::memcpy(nonce.data(), secure.data() + 20, nonce.size());
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

}  // namespace edrstore::security
