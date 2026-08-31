cmake_minimum_required(VERSION 3.16)
project(EDRStoreSecurityTools LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -Wall")

find_package(OpenSSL REQUIRED COMPONENTS Crypto)

set(EDRSTORE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
include_directories("${EDRSTORE_ROOT}/include")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${EDRSTORE_ROOT}/bin")

add_library(EDRSTORE_SECURITY_COMMON STATIC
    "${EDRSTORE_ROOT}/src/security/secure_common.cc")
target_link_libraries(EDRSTORE_SECURITY_COMMON PUBLIC OpenSSL::Crypto)

add_executable(SecureStorageTool
    secure_storage_tool.cc
    "${EDRSTORE_ROOT}/src/security/secure_storage.cc")
target_link_libraries(SecureStorageTool PRIVATE EDRSTORE_SECURITY_COMMON)

add_executable(RecipeSecurityTool
    recipe_security_tool.cc
    "${EDRSTORE_ROOT}/src/security/recipe_security.cc")
target_link_libraries(RecipeSecurityTool PRIVATE EDRSTORE_SECURITY_COMMON)

add_executable(FileIntegrityTool
    file_integrity_tool.cc
    "${EDRSTORE_ROOT}/src/security/file_integrity.cc")
target_link_libraries(FileIntegrityTool PRIVATE EDRSTORE_SECURITY_COMMON)
