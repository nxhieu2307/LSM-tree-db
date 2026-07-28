cmake_minimum_required(VERSION 3.14)
project(lsm_tree_db VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler flags
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Core library
add_library(lsm_core STATIC
    src/core/skiplist.cpp
    src/core/wal.cpp
    src/core/memtable.cpp
)

target_include_directories(lsm_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/core>
)

# Unit Test Executables
add_executable(skiplist_test tests/unit/skiplist_test.cpp)
target_link_libraries(skiplist_test PRIVATE lsm_core)

add_executable(wal_test tests/unit/wal_test.cpp)
target_link_libraries(wal_test PRIVATE lsm_core)

add_executable(memtable_test tests/unit/memtable_test.cpp)
target_link_libraries(memtable_test PRIVATE lsm_core)

# Integration Test Executables
add_executable(memtable_wal_integration_test tests/integration/memtable_wal_integration_test.cpp)
target_link_libraries(memtable_wal_integration_test PRIVATE lsm_core)

# CTest setup
enable_testing()

# Unit tests
add_test(NAME Unit_SkipList COMMAND skiplist_test)
add_test(NAME Unit_WAL COMMAND wal_test)
add_test(NAME Unit_MemTable COMMAND memtable_test)

# Integration tests
add_test(NAME Integration_MemTableWAL COMMAND memtable_wal_integration_test)

