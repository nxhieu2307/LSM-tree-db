#include "skiplist.hpp"
#include <algorithm>
#include <new>

namespace lsm {

SkipList::Node *SkipList::AllocateNode(std::string key, MemTableEntry entry,
                                       int height) {
  void *mem = ::operator new(sizeof(Node) + sizeof(Node *) * (height - 1));
  Node *node = ::new (mem) Node(std::move(key), std::move(entry));
  for (int i = 0; i < height; ++i) {
    node->forward[i] = nullptr;
  }
  return node;
}

void SkipList::FreeNode(Node *node) {
  if (node != nullptr) {
    node->~Node();
    ::operator delete(node);
  }
}

SkipList::SkipList()
    : head_(AllocateNode("", MemTableEntry{"", ValueType::kTypeValue},
                         kMaxHeight)),
      max_height_(1), rng_(std::random_device{}()) {}

SkipList::~SkipList() {
  Clear();
  FreeNode(head_);
}

void SkipList::Clear() {
  Node *current = head_->forward[0];
  while (current != nullptr) {
    Node *next = current->forward[0];
    FreeNode(current);
    current = next;
  }
  std::fill(head_->forward, head_->forward + kMaxHeight, nullptr);
  max_height_ = 1;
}

bool SkipList::Empty() const { return head_->forward[0] == nullptr; }

int SkipList::RandomHeight() {
  int height = 1;
  std::uniform_real_distribution<double> dist(0.0, 1.0);
  while (height < kMaxHeight && dist(rng_) < kBranching) {
    height++;
  }
  return height;
}

SkipList::Node *SkipList::FindGreaterOrEqual(const std::string &key,
                                             Node **update) const {
  Node *x = head_;
  int level = max_height_ - 1;
  while (true) {
    Node *next = x->forward[level];
    if (next != nullptr && next->key < key) {
      // Keep traversing along current level
      x = next;
    } else {
      // Record predecessor for this level
      if (update != nullptr) {
        update[level] = x;
      }
      if (level == 0) {
        return next;
      }
      // Drop down a level
      level--;
    }
  }
}

void SkipList::Insert(std::string key, MemTableEntry entry) {
  Node *update[kMaxHeight];
  Node *x = FindGreaterOrEqual(key, update);

  if (x != nullptr && x->key == key) {
    // Key already exists, perform update
    x->entry = std::move(entry);
    return;
  }

  int height = RandomHeight();
  if (height > max_height_) {
    for (int i = max_height_; i < height; i++) {
      update[i] = head_;
    }
    max_height_ = height;
  }

  x = AllocateNode(std::move(key), std::move(entry), height);
  for (int i = 0; i < height; i++) {
    x->forward[i] = update[i]->forward[i];
    update[i]->forward[i] = x;
  }
}

bool SkipList::Find(const std::string &key, MemTableEntry *entry) const {
  Node *x = FindGreaterOrEqual(key, nullptr);
  if (x != nullptr && x->key == key) {
    if (entry != nullptr) {
      *entry = x->entry;
    }
    return true;
  }
  return false;
}

// Iterator Implementation
SkipList::Iterator::Iterator(const SkipList &list)
    : list_(list), node_(nullptr) {}

bool SkipList::Iterator::Valid() const { return node_ != nullptr; }

void SkipList::Iterator::SeekToFirst() { node_ = list_.head_->forward[0]; }

void SkipList::Iterator::Seek(const std::string &target) {
  node_ = list_.FindGreaterOrEqual(target, nullptr);
}

void SkipList::Iterator::Next() {
  if (Valid()) {
    node_ = node_->forward[0];
  }
}

std::string SkipList::Iterator::key() const { return node_->key; }

MemTableEntry SkipList::Iterator::entry() const { return node_->entry; }

} // namespace lsm
