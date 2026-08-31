#include "security/secure_storage.h"

#include <cstdio>
#include <filesystem>
#include <string>

using edrstore::security::DirectoryStats;
using edrstore::security::SecureStorage;

namespace {

void Usage(const char* program) {
    std::fprintf(stderr,
        "Usage:\n"
        "  %s protect <OriginalContainerDir> <SecureContainerDir> [--key <path>]\n"
        "  %s unprotect <SecureContainerDir> <RecoveredContainerDir> [--key <path>]\n"
        "  %s verify <OriginalContainerDir> <RecoveredContainerDir>\n",
        program, program, program);
}

void PrintStats(const char* operation, const DirectoryStats& stats) {
    std::printf("Operation: %s\n", operation);
    std::printf("total files: %llu\n",
        static_cast<unsigned long long>(stats.total_files));
    std::printf("matched files: %llu\n",
        static_cast<unsigned long long>(stats.matched_files));
    std::printf("failed files: %llu\n",
        static_cast<unsigned long long>(stats.failed_files));
    std::printf("original total bytes: %llu\n",
        static_cast<unsigned long long>(stats.original_total_bytes));
    std::printf("output total bytes: %llu\n",
        static_cast<unsigned long long>(stats.output_total_bytes));
    if (std::string(operation) == "protect") {
        const uint64_t overhead = stats.output_total_bytes -
            stats.original_total_bytes;
        const double percent = stats.original_total_bytes == 0 ? 0.0 :
            static_cast<double>(overhead) * 100.0 /
            static_cast<double>(stats.original_total_bytes);
        std::printf("Original Container Bytes: %llu\n",
            static_cast<unsigned long long>(stats.original_total_bytes));
        std::printf("Secure Container Bytes: %llu\n",
            static_cast<unsigned long long>(stats.output_total_bytes));
        std::printf("Security Overhead Bytes: %llu\n",
            static_cast<unsigned long long>(overhead));
        std::printf("Security Overhead %%: %.6f\n", percent);
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        Usage(argv[0]);
        return 2;
    }
    const std::string operation = argv[1];
    const std::filesystem::path input = argv[2];
    const std::filesystem::path output = argv[3];
    std::filesystem::path key = "security/storage_master.key";
    if (argc == 6 && std::string(argv[4]) == "--key") {
        key = argv[5];
    } else if (argc != 4) {
        Usage(argv[0]);
        return 2;
    }

    SecureStorage storage;
    DirectoryStats stats;
    std::string error;
    bool ok = false;
    if (operation == "protect") {
        ok = storage.ProtectDirectory(input, output, key, stats, error);
    } else if (operation == "unprotect") {
        ok = storage.UnprotectDirectory(input, output, key, stats, error);
    } else if (operation == "verify" && argc == 4) {
        ok = storage.VerifyDirectories(input, output, stats, error);
    } else {
        Usage(argv[0]);
        return 2;
    }
    PrintStats(operation.c_str(), stats);
    if (!ok) {
        std::fprintf(stderr, "SecureStorageTool %s failed: %s\n",
            operation.c_str(), error.c_str());
        return 1;
    }
    return 0;
}
