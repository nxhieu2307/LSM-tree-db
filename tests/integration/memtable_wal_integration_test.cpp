#include "../../src/core/memtable.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace lsm;

void run_test(const std::string &test_name, void (*test_func)()) {
  std::cout << "[RUNNING INTEGRATION] " << test_name << "..." << std::endl;
  try {
    test_func();
    std::cout << "[ PASSED INTEGRATION] " << test_name << "\n" << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "[ FAILED INTEGRATION] " << test_name << ": " << e.what()
              << std::endl;
    std::exit(1);
  } catch (...) {
    std::cerr << "[ FAILED INTEGRATION] " << test_name << ": Unknown error"
              << std::endl;
    std::exit(1);
  }
}

void remove_file_if_exists(const std::string &filename) {
  std::remove(filename.c_str());
}

// Test 1: MemTable + WAL basic persistence and crash recovery
void test_memtable_wal_single_recovery() {
  const std::string wal_path = "test_memtable_wal_integration.log";
  remove_file_if_exists(wal_path);

  // Phase 1: Write initial dataset to MemTable (appends to WAL)
  {
    MemTable memtable(wal_path);
    assert(memtable.Put("user_100", "Alice"));
    assert(memtable.Put("user_200", "Bob"));
    assert(memtable.Put("user_300", "Charlie"));

    // Update existing key
    assert(memtable.Put("user_100", "Alice_Updated"));

    // Delete a key
    assert(memtable.Delete("user_200"));

    // Verify in-memory state before "crash"
    std::string val;
    bool is_deleted = false;
    assert(memtable.Get("user_100", &val, &is_deleted) && !is_deleted &&
           val == "Alice_Updated");
    assert(!memtable.Get("user_200", &val, &is_deleted) && is_deleted);
    assert(memtable.Get("user_300", &val, &is_deleted) && !is_deleted &&
           val == "Charlie");

    // Out-of-scope simulates process termination / crash
  }

  // Phase 2: Create a fresh MemTable initialized with the existing WAL file to
  // trigger recovery
  {
    MemTable recovered_memtable(wal_path);
    assert(!recovered_memtable.Empty());

    // Verify state after recovery
    std::string val;
    bool is_deleted = false;

    // Check user_100 has updated value
    assert(recovered_memtable.Get("user_100", &val, &is_deleted));
    assert(!is_deleted);
    assert(val == "Alice_Updated");

    // Check user_200 is marked deleted
    assert(!recovered_memtable.Get("user_200", &val, &is_deleted));
    assert(is_deleted);

    // Check user_300 exists with original value
    assert(recovered_memtable.Get("user_300", &val, &is_deleted));
    assert(!is_deleted);
    assert(val == "Charlie");

    // Check non-existent key
    assert(!recovered_memtable.Get("user_999", &val, &is_deleted));
    assert(!is_deleted);
  }

  remove_file_if_exists(wal_path);
}

// Test 2: Multi-session recovery (writes -> crash -> recover -> writes -> crash
// -> recover)
void test_memtable_wal_multi_session_recovery() {
  const std::string wal_path = "test_memtable_wal_multi_session.log";
  remove_file_if_exists(wal_path);

  // Session 1: Initial writes
  {
    MemTable session1(wal_path);
    session1.Put("session1_k1", "val1");
    session1.Put("session1_k2", "val2");
  }

  // Session 2: Recover session 1 state upon construction, perform session 2
  // writes
  {
    MemTable session2(wal_path);
    assert(session2.Count() == 2);

    session2.Put("session2_k3", "val3");
    session2.Delete("session1_k1");
  }

  // Session 3: Recover combined state of session 1 & 2 upon construction
  {
    MemTable session3(wal_path);
    assert(session3.Count() ==
           3); // session1_k1 (deleted), session1_k2, session2_k3

    std::string val;
    bool is_deleted = false;

    // session1_k1 should be deleted
    assert(!session3.Get("session1_k1", &val, &is_deleted));
    assert(is_deleted);

    // session1_k2 should still exist
    assert(session3.Get("session1_k2", &val, &is_deleted));
    assert(!is_deleted && val == "val2");

    // session2_k3 should exist
    assert(session3.Get("session2_k3", &val, &is_deleted));
    assert(!is_deleted && val == "val3");
  }

  remove_file_if_exists(wal_path);
}

// Test 3: Large volume write & recovery consistency
void test_memtable_wal_large_workload_recovery() {
  const std::string wal_path = "test_memtable_wal_large.log";
  remove_file_if_exists(wal_path);

  const int NUM_ITEMS = 1000;

  {
    MemTable memtable(wal_path);
    for (int i = 0; i < NUM_ITEMS; ++i) {
      std::string key = "key_" + std::to_string(i);
      std::string val = "value_" + std::to_string(i * 2);
      memtable.Put(key, val);
    }
    // Delete even keys
    for (int i = 0; i < NUM_ITEMS; i += 2) {
      std::string key = "key_" + std::to_string(i);
      memtable.Delete(key);
    }
  }

  // Recover in fresh MemTable
  {
    MemTable recovered(wal_path);
    assert(recovered.Count() == static_cast<size_t>(NUM_ITEMS));

    for (int i = 0; i < NUM_ITEMS; ++i) {
      std::string key = "key_" + std::to_string(i);
      std::string val;
      bool is_deleted = false;
      bool found = recovered.Get(key, &val, &is_deleted);
      if (i % 2 == 0) {
        assert(!found);
        assert(is_deleted);
      } else {
        assert(found);
        assert(!is_deleted);
        assert(val == "value_" + std::to_string(i * 2));
      }
    }
  }

  remove_file_if_exists(wal_path);
}

// Test 4: Crash simulation mid-write (truncated record trailing in WAL)
void test_memtable_wal_truncated_crash_recovery() {
  const std::string wal_path = "test_memtable_wal_truncated_crash.log";
  remove_file_if_exists(wal_path);

  // Write committed entries
  {
    MemTable memtable(wal_path);
    memtable.Put("committed_k1", "val1");
    memtable.Put("committed_k2", "val2");
  }

  // Inject truncated line simulating a power failure during a write
  {
    std::ofstream file(wal_path, std::ios::app);
    file << "{\"operation\":\"PUT\",\"key\":\"uncommitted_k3\",\"value\":\"par";
  }

  // Verify recovery succeeds and restores committed_k1 & committed_k2 without
  // failing
  {
    MemTable recovered(wal_path);
    std::string val;
    bool is_deleted = false;

    assert(recovered.Get("committed_k1", &val, &is_deleted) && !is_deleted &&
           val == "val1");
    assert(recovered.Get("committed_k2", &val, &is_deleted) && !is_deleted &&
           val == "val2");
    assert(!recovered.Get("uncommitted_k3", &val, &is_deleted));
  }

  remove_file_if_exists(wal_path);
}

int main() {
  std::cout << "=================================================="
            << std::endl;
  std::cout << "  RUNNING MEMTABLE & WAL INTEGRATION TESTS        "
            << std::endl;
  std::cout << "=================================================="
            << std::endl;

  run_test("MemTable WAL Single Recovery", test_memtable_wal_single_recovery);
  run_test("MemTable WAL Multi-Session Recovery",
           test_memtable_wal_multi_session_recovery);
  run_test("MemTable WAL Large Workload Recovery",
           test_memtable_wal_large_workload_recovery);
  run_test("MemTable WAL Truncated Crash Recovery",
           test_memtable_wal_truncated_crash_recovery);

  std::cout << "ALL MEMTABLE & WAL INTEGRATION TESTS PASSED SUCCESSFULLY!"
            << std::endl;
  return 0;
}
