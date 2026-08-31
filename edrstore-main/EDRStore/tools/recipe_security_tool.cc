#include "security/recipe_security.h"

#include <cstdio>
#include <filesystem>
#include <string>

namespace {

void Usage(const char* program) {
    std::fprintf(stderr,
        "Usage:\n"
        "  %s protect <KeyRecipe> <KeyRecipe.sec> [--key <path>] [--delete-original]\n"
        "  %s unprotect <KeyRecipe.sec> <RecoveredKeyRecipe> [--key <path>]\n",
        program, program);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        Usage(argv[0]);
        return 2;
    }
    std::filesystem::path key = "security/client_master.key";
    bool delete_original = false;
    for (int index = 4; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "--key" && index + 1 < argc) {
            key = argv[++index];
        } else if (option == "--delete-original") {
            delete_original = true;
        } else {
            Usage(argv[0]);
            return 2;
        }
    }
    edrstore::security::RecipeSecurity security;
    uint64_t input_bytes = 0;
    uint64_t output_bytes = 0;
    std::string error;
    bool ok = false;
    const std::string operation = argv[1];
    if (operation == "protect") {
        ok = security.ProtectFile(argv[2], argv[3], key, input_bytes,
            output_bytes, error);
    } else if (operation == "unprotect") {
        if (delete_original) {
            Usage(argv[0]);
            return 2;
        }
        ok = security.UnprotectFile(argv[2], argv[3], key, input_bytes,
            output_bytes, error);
    } else {
        Usage(argv[0]);
        return 2;
    }
    if (!ok) {
        std::fprintf(stderr, "RecipeSecurityTool %s failed: %s\n",
            operation.c_str(), error.c_str());
        return 1;
    }
    if (delete_original) {
        std::error_code remove_error;
        if (!std::filesystem::remove(argv[2], remove_error) || remove_error) {
            std::fprintf(stderr,
                "secure recipe was created, but original deletion failed: %s\n",
                remove_error.message().c_str());
            return 1;
        }
        std::printf("Original recipe deleted after successful protection.\n");
    }
    std::printf("Operation: %s\nInput bytes: %llu\nOutput bytes: %llu\n",
        operation.c_str(), static_cast<unsigned long long>(input_bytes),
        static_cast<unsigned long long>(output_bytes));
    return 0;
}
