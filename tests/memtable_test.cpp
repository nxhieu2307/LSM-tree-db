#include "../src/core/memtable.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace lsm;

void run_test(const std::string &test_name, void (*test_func)()) {
  std::cout << "[RUNNING] " << test_name << "..." << std::endl;
  try {
    test_func();
    std::cout << "[ PASSED] " << test_name << "\n" << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "[ FAILED] " << test_name << ": " << e.what() << std::endl;
    std::exit(1);
  } catch (...) {
    std::cerr << "[ FAILED] " << test_name << ": Unknown error" << std::endl;
    std::exit(1);
  }
}

void remove_file_if_exists(const std::string &filename) {
  std::remove(filename.c_str());
}

void test_basic_put_get_delete() {
  std::cout << "  [Step 1] Initializing empty MemTable..." << std::endl;
  MemTable memtable;

  // Put operations
  std::cout << "  [Step 2] Executing Put(\"user_1\", \"Alice\") and Put(\"user_2\", \"Bob\")..." << std::endl;
  assert(memtable.Put("user_1", "Alice"));
  assert(memtable.Put("user_2", "Bob"));

  // Get operation - found
  std::cout << "  [Step 3] Querying Get(\"user_1\") and Get(\"user_2\")..." << std::endl;
  std::string val;
  bool is_deleted = false;
  assert(memtable.Get("user_1", &val, &is_deleted));
  std::cout << "    -> Found user_1: value = \"" << val << "\", is_deleted = " << (is_deleted ? "true" : "false") << std::endl;
  assert(val == "Alice");
  assert(!is_deleted);

  assert(memtable.Get("user_2", &val, &is_deleted));
  std::cout << "    -> Found user_2: value = \"" << val << "\", is_deleted = " << (is_deleted ? "true" : "false") << std::endl;
  assert(val == "Bob");
  assert(!is_deleted);

  // Get operation - not found
  std::cout << "  [Step 4] Querying Get(\"user_3\") (Non-existent key)..." << std::endl;
  bool found_user3 = memtable.Get("user_3", &val, &is_deleted);
  std::cout << "    -> user_3 found = " << (found_user3 ? "true" : "false") << std::endl;
  assert(!found_user3);
  assert(!is_deleted);

  // Delete operation
  std::cout << "  [Step 5] Executing Delete(\"user_1\")..." << std::endl;
  assert(memtable.Delete("user_1"));

  std::cout << "  [Step 6] Querying Get(\"user_1\") after deletion..." << std::endl;
  bool found_after_del = memtable.Get("user_1", &val, &is_deleted);
  std::cout << "    -> user_1 found = " << (found_after_del ? "true" : "false") << ", tombstone (is_deleted) = " << (is_deleted ? "true" : "false") << std::endl;
  assert(!found_after_del);
  assert(is_deleted); // Tombstone should be set to true

  // Ensure user_2 remains unaffected
  std::cout << "  [Step 7] Verifying user_2 remains unaffected..." << std::endl;
  assert(memtable.Get("user_2", &val, &is_deleted));
  std::cout << "    -> Found user_2: value = \"" << val << "\", is_deleted = " << (is_deleted ? "true" : "false") << std::endl;
  assert(val == "Bob");
  assert(!is_deleted);
}

void test_overwrite_semantics() {
  std::cout << "  [Step 1] Initializing MemTable..." << std::endl;
  MemTable memtable;

  std::cout << "  [Step 2] Executing Put(\"key1\", \"val1\")..." << std::endl;
  assert(memtable.Put("key1", "val1"));
  std::string val;
  assert(memtable.Get("key1", &val));
  std::cout << "    -> key1 = \"" << val << "\"" << std::endl;
  assert(val == "val1");

  std::cout << "  [Step 3] Overwriting key1 with Put(\"key1\", \"val2\")..." << std::endl;
  assert(memtable.Put("key1", "val2"));
  assert(memtable.Get("key1", &val));
  std::cout << "    -> key1 updated = \"" << val << "\"" << std::endl;
  assert(val == "val2");

  std::cout << "  [Step 4] Overwriting key1 with Delete(\"key1\")..." << std::endl;
  assert(memtable.Delete("key1"));
  bool is_deleted = false;
  bool found = memtable.Get("key1", &val, &is_deleted);
  std::cout << "    -> key1 found = " << (found ? "true" : "false") << ", tombstone = " << (is_deleted ? "true" : "false") << std::endl;
  assert(!found);
  assert(is_deleted);
}

void test_wal_write_integration() {
  const std::string wal_file = "test_memtable_wal.wal";
  remove_file_if_exists(wal_file);

  std::cout << "  [Step 1] Creating MemTable with WAL file: " << wal_file << std::endl;
  {
    MemTable memtable(wal_file);
    std::cout << "  [Step 2] Writing entries to MemTable & WAL (Put alpha=100, Put beta=200, Delete alpha)..." << std::endl;
    assert(memtable.Put("alpha", "100"));
    assert(memtable.Put("beta", "200"));
    assert(memtable.Delete("alpha"));
  }

  std::cout << "  [Step 3] Verifying WAL file output on disk..." << std::endl;
  std::ifstream file(wal_file);
  assert(file.is_open());
  std::string line;
  std::vector<std::string> lines;
  while (std::getline(file, line)) {
    lines.push_back(line);
    std::cout << "    -> WAL Line: " << line << std::endl;
  }
  assert(lines.size() == 3);
  assert(lines[0].find("PUT") != std::string::npos);
  assert(lines[0].find("alpha") != std::string::npos);
  assert(lines[0].find("100") != std::string::npos);

  assert(lines[1].find("PUT") != std::string::npos);
  assert(lines[1].find("beta") != std::string::npos);
  assert(lines[1].find("200") != std::string::npos);

  assert(lines[2].find("DELETE") != std::string::npos);
  assert(lines[2].find("alpha") != std::string::npos);

  file.close();
  remove_file_if_exists(wal_file);
  std::cout << "  [Step 4] WAL integration verified successfully!" << std::endl;
}

int main() {
  std::cout << "==================================================" << std::endl;
  std::cout << "  Starting MemTable Basic Operations Test Suite" << std::endl;
  std::cout << "==================================================" << std::endl;

  run_test("Basic Put, Get, Delete Operations", test_basic_put_get_delete);
  run_test("Overwrite Semantics", test_overwrite_semantics);
  run_test("WAL Write Integration", test_wal_write_integration);

  std::cout << "==================================================" << std::endl;
  std::cout << "  All MemTable tests passed successfully!" << std::endl;
  std::cout << "==================================================" << std::endl;
  return 0;
}
