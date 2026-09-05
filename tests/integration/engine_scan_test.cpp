#include "../../src/core/engine.hpp"
#include <cassert>
#include <cstdio>
#include <filesystem>
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
    std::cerr << "[ FAILED INTEGRATION] " << test_name << ": " << e.what() << std::endl;
    std::exit(1);
  } catch (...) {
    std::cerr << "[ FAILED INTEGRATION] " << test_name << ": Unknown error" << std::endl;
    std::exit(1);
  }
}

void cleanup_test_dir(const std::string &dir) {
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
}

// Test 1: MergedScanMemTableAndSSTables
// Write keys a, c, e to StorageEngine and trigger a flush to create SSTable 1.
// Write keys b, d to StorageEngine and keep them in the active MemTable.
// Run Scan("a", "e") and assert strict sorted ordering {"a", "b", "c", "d", "e"}.
void test_merged_scan_memtable_and_sstables() {
  const std::string db_dir = "test_merged_scan_dir";
  const std::string wal_path = db_dir + "/wal.log";
  cleanup_test_dir(db_dir);

  {
    StorageEngine engine(4096, wal_path, db_dir);

    // Write keys a, c, e and flush to SSTable
    assert(engine.Put("a", "val_a"));
    assert(engine.Put("c", "val_c"));
    assert(engine.Put("e", "val_e"));
    engine.FlushMemTable();

    // Write keys b, d to remain in active MemTable
    assert(engine.Put("b", "val_b"));
    assert(engine.Put("d", "val_d"));

    // Run Scan("a", "e")
    auto results = engine.Scan("a", "e");

    // Assert the returned list strictly contains {"a", "b", "c", "d", "e"} in lexicographical order
    assert(results.size() == 5);
    assert(results[0].first == "a" && results[0].second == "val_a");
    assert(results[1].first == "b" && results[1].second == "val_b");
    assert(results[2].first == "c" && results[2].second == "val_c");
    assert(results[3].first == "d" && results[3].second == "val_d");
    assert(results[4].first == "e" && results[4].second == "val_e");
  }

  cleanup_test_dir(db_dir);
  std::filesystem::remove_all(db_dir);
}

// Test 2: ScanDuplicateResolutionAndTombstones
// Flush key k1 -> v1_old and k2 -> v2_old to an SSTable.
// In the active MemTable, update k1 -> v1_new and delete k2 via Delete("k2").
// Add new key k3 -> v3 to the MemTable.
// Run Scan("k1", "k3") and assert k1 returns v1_new, k2 is filtered out, and k3 returns v3.
void test_scan_duplicate_resolution_and_tombstones() {
  const std::string db_dir = "test_scan_tombstones_dir";
  const std::string wal_path = db_dir + "/wal.log";
  cleanup_test_dir(db_dir);

  {
    StorageEngine engine(4096, wal_path, db_dir);

    // Flush k1 -> v1_old and k2 -> v2_old to SSTable
    assert(engine.Put("k1", "v1_old"));
    assert(engine.Put("k2", "v2_old"));
    engine.FlushMemTable();

    // Update k1 -> v1_new and delete k2 in active MemTable
    assert(engine.Put("k1", "v1_new"));
    assert(engine.Delete("k2"));

    // Add new key k3 -> v3 in active MemTable
    assert(engine.Put("k3", "v3"));

    // Run Scan("k1", "k3")
    auto results = engine.Scan("k1", "k3");

    // Assert k1 has newest value, k2 is filtered out (tombstone), k3 has v3
    assert(results.size() == 2);
    assert(results[0].first == "k1" && results[0].second == "v1_new");
    assert(results[1].first == "k3" && results[1].second == "v3");
  }

  cleanup_test_dir(db_dir);
  std::filesystem::remove_all(db_dir);
}

// Test 3: PrefixScanEpisodicChatPattern
// Write structured keys representing chat history and unrelated data:
// chat:sess_1:001 -> msg1
// chat:sess_1:002 -> msg2
// chat:sess_2:001 -> other_msg
// doc:001 -> doc_payload
// Run PrefixScan("chat:sess_1:") and assert exactly 2 results returned matching session 1 in ascending order.
void test_prefix_scan_episodic_chat_pattern() {
  const std::string db_dir = "test_prefix_chat_dir";
  const std::string wal_path = db_dir + "/wal.log";
  cleanup_test_dir(db_dir);

  {
    StorageEngine engine(4096, wal_path, db_dir);

    // Write structured episodic chat keys
    assert(engine.Put("chat:sess_1:001", "msg1"));
    assert(engine.Put("chat:sess_1:002", "msg2"));
    assert(engine.Put("chat:sess_2:001", "other_msg"));
    assert(engine.Put("doc:001", "doc_payload"));

    // Run PrefixScan("chat:sess_1:")
    auto results = engine.PrefixScan("chat:sess_1:");

    // Assert exactly 2 results are returned matching session 1 in ascending chronological sequence
    assert(results.size() == 2);
    assert(results[0].first == "chat:sess_1:001" && results[0].second == "msg1");
    assert(results[1].first == "chat:sess_1:002" && results[1].second == "msg2");

    // Additional checks for session 2 and doc
    auto sess2_res = engine.PrefixScan("chat:sess_2:");
    assert(sess2_res.size() == 1);
    assert(sess2_res[0].first == "chat:sess_2:001" && sess2_res[0].second == "other_msg");

    auto doc_res = engine.PrefixScan("doc:");
    assert(doc_res.size() == 1);
    assert(doc_res[0].first == "doc:001" && doc_res[0].second == "doc_payload");
  }

  cleanup_test_dir(db_dir);
  std::filesystem::remove_all(db_dir);
}

// Test 4: ScanBoundsAndLimit
// Ingest keys 10 through 20.
// Test bounded scan: Scan("12", "16") returns exactly 12 through 16.
// Test limit parameter: Scan("10", "20", /*limit=*/3) returns exactly 3 records.
void test_scan_bounds_and_limit() {
  const std::string db_dir = "test_scan_bounds_dir";
  const std::string wal_path = db_dir + "/wal.log";
  cleanup_test_dir(db_dir);

  {
    StorageEngine engine(4096, wal_path, db_dir);

    // Ingest keys 10 through 20
    for (int i = 10; i <= 20; ++i) {
      assert(engine.Put(std::to_string(i), "v_" + std::to_string(i)));
    }

    // Flush to disk
    engine.FlushMemTable();

    // 1. Test bounded scan: Scan("12", "16") returns exactly 12 through 16
    auto bounded_res = engine.Scan("12", "16");
    assert(bounded_res.size() == 5);
    for (size_t idx = 0; idx < bounded_res.size(); ++idx) {
      std::string expected_k = std::to_string(12 + idx);
      std::string expected_v = "v_" + std::to_string(12 + idx);
      assert(bounded_res[idx].first == expected_k);
      assert(bounded_res[idx].second == expected_v);
    }

    // 2. Test limit parameter: Scan("10", "20", 3) returns exactly 3 records
    auto limit_res = engine.Scan("10", "20", 3);
    assert(limit_res.size() == 3);
    assert(limit_res[0].first == "10" && limit_res[0].second == "v_10");
    assert(limit_res[1].first == "11" && limit_res[1].second == "v_11");
    assert(limit_res[2].first == "12" && limit_res[2].second == "v_12");
  }

  cleanup_test_dir(db_dir);
  std::filesystem::remove_all(db_dir);
}

int main() {
  std::cout << "==================================================" << std::endl;
  std::cout << "  RUNNING STORAGE ENGINE SCAN INTEGRATION TESTS   " << std::endl;
  std::cout << "==================================================" << std::endl;

  run_test("MergedScanMemTableAndSSTables", test_merged_scan_memtable_and_sstables);
  run_test("ScanDuplicateResolutionAndTombstones", test_scan_duplicate_resolution_and_tombstones);
  run_test("PrefixScanEpisodicChatPattern", test_prefix_scan_episodic_chat_pattern);
  run_test("ScanBoundsAndLimit", test_scan_bounds_and_limit);

  std::cout << "ALL STORAGE ENGINE SCAN INTEGRATION TESTS PASSED!" << std::endl;
  return 0;
}
