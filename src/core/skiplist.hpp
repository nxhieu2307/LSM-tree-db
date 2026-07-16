#pragma once

#include <random>
#include <string>
#include <vector>

namespace lsm {

// Represents the value status: normal entry or a tombstone indicating deletion
enum class ValueType : uint8_t { kTypeValue = 0x1, kTypeDeletion = 0x2 };

// Represents an entry in the SkipList / MemTable
struct MemTableEntry {
  std::string value;
  ValueType type;
};

class SkipList {
public:
  // Node structure inside SkipList
  struct Node {
    std::string key;
    MemTableEntry entry;
    // Pointers to the next nodes at each level.
    // Level 0 corresponds to forward[0], Level 1 to forward[1], etc.
    // Trailing array of forward pointers.
    Node* forward[1];

    Node(std::string k, MemTableEntry e)
        : key(std::move(k)), entry(std::move(e)) {}
  };

  SkipList();
  ~SkipList();

  // Disallow copy/move to prevent safety issues with pointer references
  SkipList(const SkipList &) = delete;
  SkipList &operator=(const SkipList &) = delete;
  SkipList(SkipList &&) = delete;
  SkipList &operator=(SkipList &&) = delete;

  // Insert or update a key-value entry.
  // If key already exists, updates the value and type.
  void Insert(std::string key, MemTableEntry entry);

  // Find a key's entry. Returns true if found, and populates entry if not
  // nullptr.
  bool Find(const std::string &key, MemTableEntry *entry) const;

  // Clear the SkipList, deallocating all nodes.
  void Clear();

  // Check if the SkipList is empty.
  bool Empty() const;

  // Iterator interface to scan the SkipList in sorted order
  class Iterator {
  public:
    explicit Iterator(const SkipList &list);
    ~Iterator() = default;

    // Returns true if the iterator is positioned at a valid node.
    bool Valid() const;

    // Position the iterator at the first node in the SkipList.
    void SeekToFirst();

    // Position the iterator at the first node whose key is >= target.
    void Seek(const std::string &target);

    // Advance the iterator to the next node.
    void Next();

    // Return the key of the current node.
    // REQUIRES: Valid()
    std::string key() const;

    // Return the entry of the current node.
    // REQUIRES: Valid()
    MemTableEntry entry() const;

  private:
    const SkipList &list_;
    Node *node_;
  };

private:
  static constexpr int kMaxHeight = 16;
  static constexpr double kBranching = 0.25;

  Node *head_;
  int max_height_;

  // Pseudo-random generator for height assignment
  mutable std::mt19937 rng_;

  // Generate a random level height for a new node
  int RandomHeight();

  // Custom allocator/deallocator for nodes (trailing array optimization)
  static Node* AllocateNode(std::string key, MemTableEntry entry, int height);
  static void FreeNode(Node* node);

  // Internal traversal helper.
  // Finds the first node with key >= target.
  // If update is non-null, populates update[i] with the node that points
  // to the returned node (or a key >= target) at level i.
  Node *FindGreaterOrEqual(const std::string &key, Node **update) const;
};

} // namespace lsm
