#include "../src/core/compactor.hpp"
#include "../src/core/sstable_builder.hpp"
#include "../src/core/sstable_iterator.hpp"
#include "../src/core/sstable_reader.hpp"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
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

void cleanup_files(const std::vector<std::string> &files) {
  for (const auto &f : files) {
    std::error_code ec;
    std::filesystem::remove(f, ec);
  }
}

// Test 1: Merging 3 SSTables with overlapping keys, updates, and tombstones
void test_compactor_three_way_merge_and_deduplication() {
  std::string sst1 = "compactor_sstable_1.sst"; // Oldest: file_id = 1
  std::string sst2 = "compactor_sstable_2.sst"; // Middle: file_id = 2
  std::string sst3 = "compactor_sstable_3.sst"; // Newest: file_id = 3
  std::string out_sst = "compactor_merged_out.sst";

  cleanup_files({sst1, sst2, sst3, out_sst});

  // SSTable 1 (Oldest, file_id = 1):
  // "apple" -> "fruit_v1"
  // "banana" -> "yellow_v1"
  // "cherry" -> "red_v1"
  // "date" -> "sweet_v1"
  {
    SSTableBuilder b1(sst1);
    assert(b1.Add("apple", "fruit_v1"));
    assert(b1.Add("banana", "yellow_v1"));
    assert(b1.Add("cherry", "red_v1"));
    assert(b1.Add("date", "sweet_v1"));
    assert(b1.Finish());
  }

  // SSTable 2 (Middle, file_id = 2):
  // "banana" -> "yellow_v2" (updated)
  // "date" -> "" [is_deleted = true] (deleted)
  // "elderberry" -> "purple_v1" (new key)
  {
    SSTableBuilder b2(sst2);
    assert(b2.Add("banana", "yellow_v2"));
    assert(b2.Add("date", "", true));
    assert(b2.Add("elderberry", "purple_v1"));
    assert(b2.Finish());
  }

  // SSTable 3 (Newest, file_id = 3):
  // "apple" -> "fruit_v3" (updated)
  // "cherry" -> "" [is_deleted = true] (deleted)
  // "fig" -> "brown_v1" (new key)
  {
    SSTableBuilder b3(sst3);
    assert(b3.Add("apple", "fruit_v3"));
    assert(b3.Add("cherry", "", true));
    assert(b3.Add("fig", "brown_v1"));
    assert(b3.Finish());
  }

  // Open SSTableIterator instances
  auto it1 = std::make_shared<SSTableIterator>(sst1);
  auto it2 = std::make_shared<SSTableIterator>(sst2);
  auto it3 = std::make_shared<SSTableIterator>(sst3);

  std::vector<CompactorInput> inputs = {
      {1, it1},
      {2, it2},
      {3, it3}
  };

  // Perform 3-way compaction with purge_tombstones = true
  assert(Compactor::Compact(inputs, out_sst, 4096, true));

  // Verify resulting SSTable using SSTableReader and SSTableIterator
  {
    SSTableReader reader(out_sst);
    std::string val;
    bool is_deleted = false;

    // 1. "apple": updated in newest SSTable 3 -> "fruit_v3"
    assert(reader.Get("apple", &val, &is_deleted));
    assert(!is_deleted && val == "fruit_v3");

    // 2. "banana": updated in middle SSTable 2 -> "yellow_v2"
    assert(reader.Get("banana", &val, &is_deleted));
    assert(!is_deleted && val == "yellow_v2");

    // 3. "cherry": deleted in newest SSTable 3 -> purged (Get returns false)
    assert(!reader.Get("cherry", &val, &is_deleted));

    // 4. "date": deleted in middle SSTable 2 -> purged (Get returns false)
    assert(!reader.Get("date", &val, &is_deleted));

    // 5. "elderberry": introduced in middle SSTable 2 -> "purple_v1"
    assert(reader.Get("elderberry", &val, &is_deleted));
    assert(!is_deleted && val == "purple_v1");

    // 6. "fig": introduced in newest SSTable 3 -> "brown_v1"
    assert(reader.Get("fig", &val, &is_deleted));
    assert(!is_deleted && val == "brown_v1");

    // Total surviving active entries should be 4 ("apple", "banana", "elderberry", "fig")
    assert(reader.footer().entry_count == 4);
  }

  // Verify strict lexicographical iteration order via SSTableIterator
  {
    SSTableIterator iter(out_sst);
    iter.SeekToFirst();

    std::vector<std::pair<std::string, std::string>> expected_records = {
        {"apple", "fruit_v3"},
        {"banana", "yellow_v2"},
        {"elderberry", "purple_v1"},
        {"fig", "brown_v1"}};

    size_t count = 0;
    std::string prev_key = "";
    while (iter.Valid()) {
      assert(count < expected_records.size());
      assert(iter.Key() == expected_records[count].first);
      assert(iter.Value() == expected_records[count].second);
      assert(!iter.IsDeleted());

      if (!prev_key.empty()) {
        assert(iter.Key() > prev_key);
      }
      prev_key = iter.Key();

      count++;
      iter.Next();
    }
    assert(count == expected_records.size());
  }

  cleanup_files({sst1, sst2, sst3, out_sst});
}

// Test 2: Verify tombstone preservation when purge_tombstones is false
void test_compactor_tombstone_preservation_when_not_purged() {
  std::string sst1 = "compactor_nopurge_1.sst";
  std::string sst2 = "compactor_nopurge_2.sst";
  std::string out_sst = "compactor_nopurge_out.sst";

  cleanup_files({sst1, sst2, out_sst});

  {
    SSTableBuilder b1(sst1);
    b1.Add("k1", "val1", false);
    b1.Add("k2", "val2", false);
    assert(b1.Finish());
  }

  {
    SSTableBuilder b2(sst2);
    b2.Add("k1", "", true); // Delete k1 in newer generation
    assert(b2.Finish());
  }

  auto it1 = std::make_shared<SSTableIterator>(sst1);
  auto it2 = std::make_shared<SSTableIterator>(sst2);

  std::vector<CompactorInput> inputs = {
      {10, it1},
      {20, it2}
  };

  // Compact with purge_tombstones = false
  assert(Compactor::Compact(inputs, out_sst, 4096, false));

  {
    SSTableReader reader(out_sst);
    std::string val;
    bool is_deleted = false;

    // k1 tombstone must be preserved
    assert(reader.Get("k1", &val, &is_deleted));
    assert(is_deleted);

    // k2 alive
    assert(reader.Get("k2", &val, &is_deleted));
    assert(!is_deleted && val == "val2");

    assert(reader.footer().entry_count == 2);
  }

  cleanup_files({sst1, sst2, out_sst});
}

int main() {
  std::cout << "=================================================="
            << std::endl;
  std::cout << "  RUNNING COMPACTOR UNIT TESTS                   "
            << std::endl;
  std::cout << "=================================================="
            << std::endl;

  run_test("Compactor 3-Way Merge, Deduplication & Tombstone Purging",
           test_compactor_three_way_merge_and_deduplication);
  run_test("Compactor Tombstone Preservation When Not Purged",
           test_compactor_tombstone_preservation_when_not_purged);

  std::cout << "ALL COMPACTOR UNIT TESTS PASSED SUCCESSFULLY!"
            << std::endl;
  return 0;
}
