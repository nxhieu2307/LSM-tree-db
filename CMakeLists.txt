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

find_package(Threads REQUIRED)

# Core library
add_library(lsm_core STATIC
    src/core/skiplist.cpp
    src/core/wal.cpp
    src/core/memtable.cpp
    src/core/sstable_builder.cpp
    src/core/sstable_reader.cpp
    src/core/manifest.cpp
    src/core/engine.cpp
)

target_include_directories(lsm_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/core>
)

# CLI Binary Executable
add_executable(lsm_db
    src/main.cpp
    src/core/skiplist.cpp
    src/core/wal.cpp
    src/core/memtable.cpp
    src/core/sstable_builder.cpp
    src/core/sstable_reader.cpp
    src/core/manifest.cpp
    src/core/engine.cpp
)
target_include_directories(lsm_db PRIVATE src/core)
target_link_libraries(lsm_db PRIVATE lsm_core Threads::Threads)

# Unit Test Executables
add_executable(skiplist_test tests/unit/skiplist_test.cpp)
target_link_libraries(skiplist_test PRIVATE lsm_core)

add_executable(wal_test tests/unit/wal_test.cpp)
target_link_libraries(wal_test PRIVATE lsm_core)

add_executable(memtable_test tests/unit/memtable_test.cpp)
target_link_libraries(memtable_test PRIVATE lsm_core)

add_executable(sstable_builder_test tests/unit/sstable_builder_test.cpp)
target_link_libraries(sstable_builder_test PRIVATE lsm_core)

add_executable(sstable_reader_test tests/unit/sstable_reader_test.cpp)
target_link_libraries(sstable_reader_test PRIVATE lsm_core)

# Integration Test Executables
add_executable(memtable_wal_integration_test tests/integration/memtable_wal_integration_test.cpp)
target_link_libraries(memtable_wal_integration_test PRIVATE lsm_core)

add_executable(integration_engine_sstable tests/integration/engine_sstable_integration_test.cpp)
target_link_libraries(integration_engine_sstable PRIVATE lsm_core)

add_executable(integration_engine_recovery tests/integration/engine_recovery_test.cpp)
target_link_libraries(integration_engine_recovery PRIVATE lsm_core)

# CTest setup
enable_testing()

# Unit tests
add_test(NAME Unit_SkipList COMMAND skiplist_test)
add_test(NAME Unit_WAL COMMAND wal_test)
add_test(NAME Unit_MemTable COMMAND memtable_test)
add_test(NAME Unit_SSTableBuilder COMMAND sstable_builder_test)
add_test(NAME Unit_SSTableReader COMMAND sstable_reader_test)

# Integration tests
add_test(NAME Integration_MemTableWAL COMMAND memtable_wal_integration_test)
add_test(NAME Integration_EngineSSTable COMMAND integration_engine_sstable)
add_test(NAME Integration_EngineRecovery COMMAND integration_engine_recovery)




