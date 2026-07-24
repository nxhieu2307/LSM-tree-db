cmake_minimum_required(VERSION 3.14)
project(lsm_tree_db VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

# Test executables
add_executable(skiplist_test tests/skiplist_test.cpp)
target_link_libraries(skiplist_test PRIVATE lsm_core)

add_executable(wal_test tests/wal_test.cpp)
target_link_libraries(wal_test PRIVATE lsm_core)

add_executable(memtable_test tests/memtable_test.cpp)
target_link_libraries(memtable_test PRIVATE lsm_core)

# CTest setup
enable_testing()
add_test(NAME SkipListTest COMMAND skiplist_test)
add_test(NAME WALTest COMMAND wal_test)
add_test(NAME MemTableTest COMMAND memtable_test)
