#include "../../src/core/memtable.hpp"
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
  assert(memtable.Empty());
  assert(memtable.Count() == 0);

  // Put operations
  std::cout << "  [Step 2] Executing Put(\"user_1\", \"Alice\") and "
               "Put(\"user_2\", \"Bob\")..."
            << std::endl;
  assert(memtable.Put("user_1", "Alice"));
  assert(memtable.Put("user_2", "Bob"));
  assert(!memtable.Empty());
  assert(memtable.Count() == 2);

  // Get operation - found
  std::cout << "  [Step 3] Querying Get(\"user_1\") and Get(\"user_2\")..."
            << std::endl;
  std::string val;
  bool is_deleted = false;
  assert(memtable.Get("user_1", &val, &is_deleted));
  std::cout << "    -> Found user_1: value = \"" << val
            << "\", is_deleted = " << (is_deleted ? "true" : "false")
            << std::endl;
  assert(val == "Alice");
  assert(!is_deleted);

  assert(memtable.Get("user_2", &val, &is_deleted));
  std::cout << "    -> Found user_2: value = \"" << val
            << "\", is_deleted = " << (is_deleted ? "true" : "false")
            << std::endl;
  assert(val == "Bob");
  assert(!is_deleted);

  // Get operation - not found
  std::cout << "  [Step 4] Querying Get(\"user_3\") (Non-existent key)..."
            << std::endl;
  bool found_user3 = memtable.Get("user_3", &val, &is_deleted);
  std::cout << "    -> user_3 found = " << (found_user3 ? "true" : "false")
            << std::endl;
  assert(!found_user3);
  assert(!is_deleted);

  // Delete operation
  std::cout << "  [Step 5] Executing Delete(\"user_1\")..." << std::endl;
  assert(memtable.Delete("user_1"));

  std::cout << "  [Step 6] Querying Get(\"user_1\") after deletion..."
            << std::endl;
  bool found_after_del = memtable.Get("user_1", &val, &is_deleted);
  std::cout << "    -> user_1 found = " << (found_after_del ? "true" : "false")
            << ", tombstone (is_deleted) = " << (is_deleted ? "true" : "false")
            << std::endl;
  assert(!found_after_del);
  assert(is_deleted); // Tombstone should be set to true

  // Ensure user_2 remains unaffected
  std::cout << "  [Step 7] Verifying user_2 remains unaffected..." << std::endl;
  assert(memtable.Get("user_2", &val, &is_deleted));
  std::cout << "    -> Found user_2: value = \"" << val
            << "\", is_deleted = " << (is_deleted ? "true" : "false")
            << std::endl;
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
  assert(memtable.Count() == 1);

  std::cout << "  [Step 3] Overwriting key1 with Put(\"key1\", \"val2\")..."
            << std::endl;
  assert(memtable.Put("key1", "val2"));
  assert(memtable.Get("key1", &val));
  std::cout << "    -> key1 updated = \"" << val << "\"" << std::endl;
  assert(val == "val2");
  assert(memtable.Count() == 1); // Count should remain 1 on key overwrite

  std::cout << "  [Step 4] Overwriting key1 with Delete(\"key1\")..."
            << std::endl;
  assert(memtable.Delete("key1"));
  bool is_deleted = false;
  bool found = memtable.Get("key1", &val, &is_deleted);
  std::cout << "    -> key1 found = " << (found ? "true" : "false")
            << ", tombstone = " << (is_deleted ? "true" : "false") << std::endl;
  assert(!found);
  assert(is_deleted);
  assert(memtable.Count() == 1);
}

void test_wal_write_integration() {
  const std::string wal_file = "test_memtable_wal.wal";
  remove_file_if_exists(wal_file);

  std::cout << "  [Step 1] Creating MemTable with WAL file: " << wal_file
            << std::endl;
  {
    MemTable memtable(wal_file);
    std::cout << "  [Step 2] Writing entries to MemTable & WAL (Put alpha=100, "
                 "Put beta=200, Delete alpha)..."
              << std::endl;
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

void test_wal_crash_recovery() {
  const std::string wal_file = "test_memtable_recovery.wal";
  remove_file_if_exists(wal_file);

  std::cout << "  [Step 1] Writing initial dataset to MemTable & WAL..."
            << std::endl;
  {
    MemTable memtable(wal_file);
    assert(memtable.Put("k1", "v1"));
    assert(memtable.Put("k2", "v2"));
    assert(memtable.Put("k3", "v3"));
    assert(memtable.Delete("k2"));
    assert(memtable.Put("k1", "v1_updated"));
  } // MemTable closes & leaves log file on disk

  std::cout << "  [Step 2] Re-instantiating MemTable with existing WAL file to "
               "simulate recovery..."
            << std::endl;
  {
    MemTable recovered(wal_file);
    assert(recovered.Count() == 3);

    std::string val;
    bool is_deleted = false;

    // k1 should be v1_updated
    assert(recovered.Get("k1", &val, &is_deleted));
    assert(val == "v1_updated");
    assert(!is_deleted);

    // k2 should be tombstone deleted
    assert(!recovered.Get("k2", &val, &is_deleted));
    assert(is_deleted);

    // k3 should be v3
    assert(recovered.Get("k3", &val, &is_deleted));
    assert(val == "v3");
    assert(!is_deleted);

    std::cout << "    -> Successfully recovered entries: k1=" << val
              << ", k2 (deleted), k3=v3" << std::endl;
  }

  remove_file_if_exists(wal_file);
  std::cout << "  [Step 3] WAL crash recovery verified!" << std::endl;
}

void test_memory_usage_and_counting() {
  std::cout << "  [Step 1] Testing memory usage tracking..." << std::endl;
  MemTable memtable;
  assert(memtable.ApproximateMemoryUsage() == 0);

  size_t initial_mem = memtable.ApproximateMemoryUsage();
  memtable.Put("key_alpha", "value_1");
  size_t mem_after_put1 = memtable.ApproximateMemoryUsage();
  std::cout << "    -> Memory after 1 put: " << mem_after_put1 << " bytes"
            << std::endl;
  assert(mem_after_put1 > initial_mem);

  memtable.Put("key_alpha", "value_1_longer_string");
  size_t mem_after_update = memtable.ApproximateMemoryUsage();
  std::cout << "    -> Memory after updating key to longer string: "
            << mem_after_update << " bytes" << std::endl;
  assert(mem_after_update > mem_after_put1);

  memtable.Clear();
  assert(memtable.ApproximateMemoryUsage() == 0);
  assert(memtable.Count() == 0);
  assert(memtable.Empty());
  std::cout << "  [Step 2] Memory tracking reset after Clear() verified!"
            << std::endl;
}

void test_immutable_state() {
  std::cout << "  [Step 1] Testing immutable state enforcement..." << std::endl;
  MemTable memtable;
  assert(!memtable.IsImmutable());

  assert(memtable.Put("active_key", "active_val"));
  memtable.MarkImmutable();
  assert(memtable.IsImmutable());

  std::cout << "  [Step 2] Verifying Put & Delete fail on immutable MemTable..."
            << std::endl;
  assert(!memtable.Put("new_key", "new_val"));
  assert(!memtable.Delete("active_key"));

  std::cout << "  [Step 3] Verifying Get still works on immutable MemTable..."
            << std::endl;
  std::string val;
  assert(memtable.Get("active_key", &val));
  assert(val == "active_val");
}

void test_iterator_interface() {
  std::cout << "  [Step 1] Populating MemTable for iterator test..."
            << std::endl;
  MemTable memtable;
  memtable.Put("c_key", "c_val");
  memtable.Put("a_key", "a_val");
  memtable.Put("b_key", "b_val");
  memtable.Delete("d_key");

  std::cout << "  [Step 2] Testing forward iterator traversal..." << std::endl;
  auto iter = memtable.NewIterator();
  std::vector<std::string> keys;
  std::vector<std::string> values;

  for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
    keys.push_back(iter->key());
    if (iter->IsDeleted()) {
      values.push_back("<DELETED>");
    } else {
      values.push_back(iter->value());
    }
  }

  assert(keys.size() == 4);
  assert(keys[0] == "a_key" && values[0] == "a_val");
  assert(keys[1] == "b_key" && values[1] == "b_val");
  assert(keys[2] == "c_key" && values[2] == "c_val");
  assert(keys[3] == "d_key" && values[3] == "<DELETED>");

  std::cout << "  [Step 3] Testing Seek() lower bound lookup..." << std::endl;
  iter->Seek("b_key");
  assert(iter->Valid());
  assert(iter->key() == "b_key");

  iter->Seek("b_mid");
  assert(iter->Valid());
  assert(iter->key() == "c_key");

  iter->Seek("z_key");
  assert(!iter->Valid());

  std::cout << "  [Step 4] Iterator interface verified!" << std::endl;
}

int main() {
  std::cout << "=================================================="
            << std::endl;
  std::cout << "  Starting Complete MemTable Operations Test Suite"
            << std::endl;
  std::cout << "=================================================="
            << std::endl;

  run_test("Basic Put, Get, Delete Operations", test_basic_put_get_delete);
  run_test("Overwrite Semantics", test_overwrite_semantics);
  run_test("WAL Write Integration", test_wal_write_integration);
  run_test("WAL Crash Recovery", test_wal_crash_recovery);
  run_test("Memory Usage & Entry Counting", test_memory_usage_and_counting);
  run_test("Immutable State Enforcement", test_immutable_state);
  run_test("Iterator Interface", test_iterator_interface);

  std::cout << "=================================================="
            << std::endl;
  std::cout << "  All MemTable tests passed successfully!" << std::endl;
  std::cout << "=================================================="
            << std::endl;
  return 0;
}
