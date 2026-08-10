#include "../../src/core/bloom_filter.hpp"
#include <cassert>
#include <iostream>
#include <sstream>
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

void test_basic_add_and_may_contain() {
  std::cout << "  [Step 1] Constructing default BloomFilter (expected=1000, fp=0.01)..." << std::endl;
  BloomFilter bf(1000, 0.01);

  assert(bf.GetExpectedItems() == 1000);
  assert(bf.GetFPRate() == 0.01);
  assert(bf.GetBitCount() > 0);
  assert(bf.GetHashCount() > 0);

  std::cout << "  [Step 2] Adding 500 keys to BloomFilter..." << std::endl;
  std::vector<std::string> keys;
  keys.reserve(500);
  for (int i = 0; i < 500; ++i) {
    keys.push_back("user_key_" + std::to_string(i));
    bf.Add(keys.back());
  }

  std::cout << "  [Step 3] Verifying zero false negatives for all inserted keys..." << std::endl;
  for (const auto &key : keys) {
    bool found = bf.MayContain(key);
    assert(found && "Bloom filter MUST return true for all inserted keys (0 false negatives)");
  }
}

void test_serialization_deserialization() {
  std::cout << "  [Step 1] Creating and populating original BloomFilter..." << std::endl;
  BloomFilter original(1000, 0.01);
  std::vector<std::string> inserted_keys = {"alpha", "beta", "gamma", "delta", "epsilon"};
  for (const auto &key : inserted_keys) {
    original.Add(key);
  }

  std::cout << "  [Step 2] Serializing original BloomFilter to std::stringstream..." << std::endl;
  std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
  original.Serialize(ss);

  std::cout << "  [Step 3] Deserializing into a fresh BloomFilter instance..." << std::endl;
  BloomFilter restored(1, 0.5);
  restored.Deserialize(ss);

  std::cout << "  [Step 4] Verifying restored metadata and bit state..." << std::endl;
  assert(restored.GetExpectedItems() == original.GetExpectedItems());
  assert(restored.GetFPRate() == original.GetFPRate());
  assert(restored.GetBitCount() == original.GetBitCount());
  assert(restored.GetHashCount() == original.GetHashCount());
  assert(restored.GetBits() == original.GetBits());

  for (const auto &key : inserted_keys) {
    assert(restored.MayContain(key));
  }

  std::vector<std::string> test_uninserted = {"zebra", "lion", "tiger"};
  for (const auto &key : test_uninserted) {
    assert(restored.MayContain(key) == original.MayContain(key));
  }
}

void test_false_positive_rate() {
  std::cout << "  [Step 1] Initializing BloomFilter for N=1000, P=0.01..." << std::endl;
  const size_t N = 1000;
  const double target_p = 0.01;
  BloomFilter bf(N, target_p);

  std::cout << "  [Step 2] Inserting " << N << " keys..." << std::endl;
  for (size_t i = 0; i < N; ++i) {
    bf.Add("inserted_key_" + std::to_string(i));
  }

  std::cout << "  [Step 3] Testing 10,000 uninserted keys to calculate false positive rate..." << std::endl;
  const size_t NUM_UNINSERTED = 10000;
  size_t false_positives = 0;

  for (size_t i = 0; i < NUM_UNINSERTED; ++i) {
    std::string uninserted_key = "uninserted_key_" + std::to_string(i);
    if (bf.MayContain(uninserted_key)) {
      false_positives++;
    }
  }

  double actual_fp_rate = static_cast<double>(false_positives) / static_cast<double>(NUM_UNINSERTED);
  std::cout << "    -> False positives: " << false_positives << " / " << NUM_UNINSERTED
            << " (" << (actual_fp_rate * 100.0) << "%)" << std::endl;

  assert(actual_fp_rate <= 0.02 && "False positive rate exceeds 2% threshold");
}

int main() {
  std::cout << "==================================================" << std::endl;
  std::cout << "  Starting BloomFilter Unit Test Suite" << std::endl;
  std::cout << "==================================================" << std::endl;

  run_test("Basic Add and MayContain (Zero False Negatives)", test_basic_add_and_may_contain);
  run_test("Serialization and Deserialization (State Preservation)", test_serialization_deserialization);
  run_test("False Positive Rate (< 2% Threshold)", test_false_positive_rate);

  std::cout << "==================================================" << std::endl;
  std::cout << "  All BloomFilter tests passed successfully!" << std::endl;
  std::cout << "==================================================" << std::endl;
  return 0;
}
