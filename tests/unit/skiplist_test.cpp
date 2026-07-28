#include "../../src/core/skiplist.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <random>

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

void test_basic_insert_find() {
  lsm::SkipList list;
  assert(list.Empty());

  lsm::MemTableEntry entry1{"value1", lsm::ValueType::kTypeValue};
  lsm::MemTableEntry entry2{"value2", lsm::ValueType::kTypeValue};
  lsm::MemTableEntry entry3{"", lsm::ValueType::kTypeDeletion};

  list.Insert("key1", entry1);
  list.Insert("key2", entry2);
  list.Insert("key3", entry3);

  assert(!list.Empty());

  lsm::MemTableEntry found;
  
  // Find key1
  assert(list.Find("key1", &found));
  assert(found.value == "value1");
  assert(found.type == lsm::ValueType::kTypeValue);

  // Find key2
  assert(list.Find("key2", &found));
  assert(found.value == "value2");
  assert(found.type == lsm::ValueType::kTypeValue);

  // Find key3 (deleted tombstone)
  assert(list.Find("key3", &found));
  assert(found.value == "");
  assert(found.type == lsm::ValueType::kTypeDeletion);

  // Find non-existent key
  assert(!list.Find("key_non_existent", &found));
}

void test_update_existing() {
  lsm::SkipList list;
  
  list.Insert("key1", {"value1", lsm::ValueType::kTypeValue});
  lsm::MemTableEntry found;
  assert(list.Find("key1", &found));
  assert(found.value == "value1");

  // Update value
  list.Insert("key1", {"value2_updated", lsm::ValueType::kTypeValue});
  assert(list.Find("key1", &found));
  assert(found.value == "value2_updated");

  // Update to deletion tombstone
  list.Insert("key1", {"", lsm::ValueType::kTypeDeletion});
  assert(list.Find("key1", &found));
  assert(found.type == lsm::ValueType::kTypeDeletion);
}

void test_empty_and_clear() {
  lsm::SkipList list;
  assert(list.Empty());

  list.Insert("k1", {"v1", lsm::ValueType::kTypeValue});
  assert(!list.Empty());

  list.Clear();
  assert(list.Empty());

  lsm::MemTableEntry found;
  assert(!list.Find("k1", &found));

  // Reuse after clear
  list.Insert("k2", {"v2", lsm::ValueType::kTypeValue});
  assert(!list.Empty());
  assert(list.Find("k2", &found));
  assert(found.value == "v2");
}

void test_iterator_sorted_and_seek() {
  lsm::SkipList list;

  // Insert in unsorted order
  list.Insert("cherry", {"val_cherry", lsm::ValueType::kTypeValue});
  list.Insert("apple", {"val_apple", lsm::ValueType::kTypeValue});
  list.Insert("date", {"val_date", lsm::ValueType::kTypeValue});
  list.Insert("banana", {"val_banana", lsm::ValueType::kTypeValue});

  // Verify elements are sorted
  lsm::SkipList::Iterator iter(list);
  
  std::vector<std::pair<std::string, std::string>> elements;
  for (iter.SeekToFirst(); iter.Valid(); iter.Next()) {
    elements.push_back({iter.key(), iter.entry().value});
  }

  assert(elements.size() == 4);
  assert(elements[0].first == "apple");
  assert(elements[1].first == "banana");
  assert(elements[2].first == "cherry");
  assert(elements[3].first == "date");

  // Seek test: exact match
  iter.Seek("banana");
  assert(iter.Valid());
  assert(iter.key() == "banana");

  // Seek test: inexact match (points to first key >= target)
  iter.Seek("blueberry"); // between banana and cherry
  assert(iter.Valid());
  assert(iter.key() == "cherry");

  // Seek test: seek before all keys
  iter.Seek("a");
  assert(iter.Valid());
  assert(iter.key() == "apple");

  // Seek test: seek after all keys
  iter.Seek("z");
  assert(!iter.Valid());
}

void test_large_scale_random() {
  lsm::SkipList list;
  const int kNumElements = 2000;
  
  std::vector<std::string> keys;
  for (int i = 0; i < kNumElements; i++) {
    keys.push_back("key_" + std::to_string(i));
  }

  // Shuffle keys to insert randomly
  std::random_device rd;
  std::mt19937 g(rd());
  std::shuffle(keys.begin(), keys.end(), g);

  for (const auto& key : keys) {
    list.Insert(key, {"val_" + key, lsm::ValueType::kTypeValue});
  }

  // Find all elements and verify
  for (const auto& key : keys) {
    lsm::MemTableEntry found;
    assert(list.Find(key, &found));
    assert(found.value == "val_" + key);
  }

  // Verify sorted order via iterator
  lsm::SkipList::Iterator iter(list);
  std::string prev_key = "";
  int count = 0;
  for (iter.SeekToFirst(); iter.Valid(); iter.Next()) {
    if (!prev_key.empty()) {
      assert(iter.key() > prev_key);
    }
    prev_key = iter.key();
    count++;
  }
  assert(count == kNumElements);
}

int main() {
  std::cout << "Starting SkipList component tests..." << std::endl;
  
  run_test("Basic Insert and Find", test_basic_insert_find);
  run_test("Update Existing Keys", test_update_existing);
  run_test("Empty and Clear Verification", test_empty_and_clear);
  run_test("Iterator Sorted and Seek Verification", test_iterator_sorted_and_seek);
  run_test("Large Scale Random Operations", test_large_scale_random);

  std::cout << "All SkipList tests passed successfully!" << std::endl;
  return 0;
}
