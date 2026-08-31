#include "security/file_integrity.h"

#include <cstdio>
#include <filesystem>
#include <string>

namespace {

void Usage(const char* program) {
    std::fprintf(stderr,
        "Usage:\n"
        "  %s create <OriginalFile> <filename.mac.sec> [--key <path>]\n"
        "  %s verify <RecoveredFile> <filename.mac.sec> [--key <path>]\n",
        program, program);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        Usage(argv[0]);
        return 2;
    }
    std::filesystem::path key = "security/client_master.key";
    if (argc == 6 && std::string(argv[4]) == "--key") {
        key = argv[5];
    } else if (argc != 4) {
        Usage(argv[0]);
        return 2;
    }
    edrstore::security::FileIntegrity integrity;
    uint64_t file_size = 0;
    std::string error;
    bool ok = false;
    const std::string operation = argv[1];
    if (operation == "create") {
        ok = integrity.Create(argv[2], argv[3], key, file_size, error);
    } else if (operation == "verify") {
        ok = integrity.Verify(argv[2], argv[3], key, file_size, error);
    } else {
        Usage(argv[0]);
        return 2;
    }
    if (!ok) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    std::printf("File integrity verification %s\nFile bytes: %llu\n",
        operation == "verify" ? "PASSED" : "metadata created",
        static_cast<unsigned long long>(file_size));
    return 0;
}
