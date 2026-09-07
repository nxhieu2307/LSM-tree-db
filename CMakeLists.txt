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

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Threads REQUIRED)

# Core library
add_library(lsm_core STATIC
    src/core/skiplist.cpp
    src/core/wal.cpp
    src/core/memtable.cpp
    src/core/sstable_builder.cpp
    src/core/sstable_reader.cpp
    src/core/sstable_iterator.cpp
    src/core/compactor.cpp
    src/core/manifest.cpp
    src/core/engine.cpp
    src/core/bloom_filter.cpp
    src/core/db_iterator.cpp
)
set_target_properties(lsm_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(lsm_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/core>
)

# Python Bindings (pybind11)
include(FetchContent)
FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG v2.12.0
)
FetchContent_MakeAvailable(pybind11)

pybind11_add_module(pylsm src/bindings/python_bindings.cpp)
target_include_directories(pylsm PRIVATE
    "$ENV{HOME}/.local/include"
    "$ENV{HOME}/.local/include/python3.14"
    "$ENV{HOME}/.local/include/x86_64-linux-gnu/python3.14"
    "${CMAKE_CURRENT_SOURCE_DIR}/build/py-dev/usr/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/build/py-dev/usr/include/python3.14"
    "${CMAKE_CURRENT_SOURCE_DIR}/build/py-dev/usr/include/x86_64-linux-gnu/python3.14"
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
)
target_link_libraries(pylsm PRIVATE lsm_core)
set_target_properties(pylsm PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# CLI Binary Executable
add_executable(lsm_db
    src/main.cpp
    src/core/skiplist.cpp
    src/core/wal.cpp
    src/core/memtable.cpp
    src/core/sstable_builder.cpp
    src/core/sstable_reader.cpp
    src/core/sstable_iterator.cpp
    src/core/compactor.cpp
    src/core/manifest.cpp
    src/core/engine.cpp
    src/core/bloom_filter.cpp
    src/core/db_iterator.cpp
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
add_custom_target(unit_sstable_reader DEPENDS sstable_reader_test)

add_executable(sstable_iterator_test tests/unit/sstable_iterator_test.cpp)
target_link_libraries(sstable_iterator_test PRIVATE lsm_core)
add_custom_target(unit_sstable_iterator DEPENDS sstable_iterator_test)

add_executable(unit_bloom_filter tests/unit/bloom_filter_test.cpp)
target_link_libraries(unit_bloom_filter PRIVATE lsm_core)

add_executable(compactor_test tests/unit/compactor_test.cpp)
target_link_libraries(compactor_test PRIVATE lsm_core)
add_custom_target(unit_compactor DEPENDS compactor_test)

add_executable(manifest_test tests/unit/manifest_test.cpp)
target_link_libraries(manifest_test PRIVATE lsm_core)
add_custom_target(unit_manifest DEPENDS manifest_test)

add_executable(db_iterator_test tests/unit/db_iterator_test.cpp)
target_link_libraries(db_iterator_test PRIVATE lsm_core)
add_custom_target(unit_db_iterator DEPENDS db_iterator_test)

# Integration Test Executables
add_executable(memtable_wal_integration_test tests/integration/memtable_wal_integration_test.cpp)
target_link_libraries(memtable_wal_integration_test PRIVATE lsm_core)

add_executable(integration_engine_sstable tests/integration/engine_sstable_integration_test.cpp)
target_link_libraries(integration_engine_sstable PRIVATE lsm_core)

add_executable(integration_engine_recovery tests/integration/engine_recovery_test.cpp)
target_link_libraries(integration_engine_recovery PRIVATE lsm_core)

add_executable(integration_engine_compaction tests/integration/engine_compaction_test.cpp)
target_link_libraries(integration_engine_compaction PRIVATE lsm_core)
add_custom_target(Integration_EngineCompaction DEPENDS integration_engine_compaction)

add_executable(integration_engine_scan tests/integration/engine_scan_test.cpp)
target_link_libraries(integration_engine_scan PRIVATE lsm_core)
add_custom_target(Integration_EngineScan DEPENDS integration_engine_scan)

# CTest setup
enable_testing()

# Unit tests
add_test(NAME Unit_SkipList COMMAND skiplist_test)
add_test(NAME Unit_WAL COMMAND wal_test)
add_test(NAME Unit_MemTable COMMAND memtable_test)
add_test(NAME Unit_SSTableBuilder COMMAND sstable_builder_test)
add_test(NAME Unit_SSTableReader COMMAND sstable_reader_test)
add_test(NAME Unit_SSTableIterator COMMAND sstable_iterator_test)
add_test(NAME Unit_BloomFilter COMMAND unit_bloom_filter)
add_test(NAME Unit_Compactor COMMAND compactor_test)
add_test(NAME Unit_Manifest COMMAND manifest_test)
add_test(NAME Unit_DBIterator COMMAND db_iterator_test)

# Integration tests
add_test(NAME Integration_MemTableWAL COMMAND memtable_wal_integration_test)
add_test(NAME Integration_EngineSSTable COMMAND integration_engine_sstable)
add_test(NAME Integration_EngineRecovery COMMAND integration_engine_recovery)
add_test(NAME Integration_EngineCompaction COMMAND integration_engine_compaction)
add_test(NAME Integration_EngineScan COMMAND integration_engine_scan)

# Python bindings & LTM tests
add_test(NAME Python_PyLSM COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_pylsm.py)
set_tests_properties(Python_PyLSM PROPERTIES ENVIRONMENT "PYTHONPATH=${CMAKE_BINARY_DIR}:${CMAKE_CURRENT_SOURCE_DIR}/python")

add_test(NAME Python_LTMStore COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_ltm_store.py)
set_tests_properties(Python_LTMStore PROPERTIES ENVIRONMENT "PYTHONPATH=${CMAKE_BINARY_DIR}:${CMAKE_CURRENT_SOURCE_DIR}/python")








