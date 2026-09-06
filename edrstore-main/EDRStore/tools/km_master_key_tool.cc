#include "../include/security/km_master_key.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>

#include <array>
#include <cstring>
#include <cstdio>
#include <string>

using edrstore::security::KMMasterKey;

namespace {

void Usage(const char* program) {
    std::fprintf(stderr,
        "Usage:\n"
        "  %s init <security/km/km_master.bin>\n"
        "  %s compare <km_master_A> <km_master_B>\n"
        "  %s determinism <km_master>\n",
        program, program, program);
}

bool ComputeLegacyTestSeed(const KMMasterKey& master,
    std::array<uint8_t, 32>& output) {
    std::array<uint8_t, 32 + sizeof(uint64_t) * 3> input{};
    const std::array<uint64_t, 3> features = {
        0x1020304050607080ULL,
        0x1122334455667788ULL,
        0x8877665544332211ULL};
    std::memcpy(input.data(), master.GlobalSecret(), 32);
    std::memcpy(input.data() + 32, features.data(),
        sizeof(uint64_t) * features.size());
    unsigned int output_size = 0;
    const bool ok = EVP_Digest(input.data(), input.size(), output.data(),
        &output_size, EVP_sha256(), nullptr) == 1 &&
        output_size == output.size();
    OPENSSL_cleanse(input.data(), input.size());
    return ok;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc == 3 && std::string(argv[1]) == "init") {
        KMMasterKey master;
        std::string error;
        if (!master.CreateNew(argv[2], error)) {
            std::fprintf(stderr, "Initialization failed: %s\n", error.c_str());
            return 1;
        }
        std::fprintf(stdout, "KM master initialized successfully.\n");
        return 0;
    }
    if (argc == 4 && std::string(argv[1]) == "compare") {
        KMMasterKey first;
        KMMasterKey second;
        std::string error;
        if (!first.LoadExisting(argv[2], error)) {
            std::fprintf(stderr, "First input rejected: %s\n", error.c_str());
            return 1;
        }
        if (!second.LoadExisting(argv[3], error)) {
            std::fprintf(stderr, "Second input rejected: %s\n", error.c_str());
            return 1;
        }
        const bool same_global = CRYPTO_memcmp(first.GlobalSecret(),
            second.GlobalSecret(), KMMasterKey::kSecretSize) == 0;
        const bool same_padding = CRYPTO_memcmp(first.PaddingSecret(),
            second.PaddingSecret(), KMMasterKey::kSecretSize) == 0;
        std::fprintf(stdout, "%s\n",
            same_global && same_padding ? "SAME" : "DIFFERENT");
        return same_global && same_padding ? 2 : 0;
    }
    if (argc == 3 && std::string(argv[1]) == "determinism") {
        KMMasterKey first_start;
        KMMasterKey second_start;
        std::string error;
        if (!first_start.LoadExisting(argv[2], error) ||
            !second_start.LoadExisting(argv[2], error)) {
            std::fprintf(stderr, "Persistent reload rejected: %s\n",
                error.c_str());
            return 1;
        }
        std::array<uint8_t, 32> first_seed{};
        std::array<uint8_t, 32> second_seed{};
        const bool generated = ComputeLegacyTestSeed(first_start, first_seed) &&
            ComputeLegacyTestSeed(second_start, second_seed);
        const bool same = generated && CRYPTO_memcmp(first_seed.data(),
            second_seed.data(), first_seed.size()) == 0;
        OPENSSL_cleanse(first_seed.data(), first_seed.size());
        OPENSSL_cleanse(second_seed.data(), second_seed.size());
        std::fprintf(stdout, "%s\n", same ? "SAME" : "DIFFERENT");
        return same ? 0 : 1;
    }
    Usage(argv[0]);
    return 2;
}
