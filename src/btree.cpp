#include "btree.h"

#include <stdexcept>

BTree::Entry::Entry(int key, int rid)
    : key(key), rid(rid) {}

BTree::Node::Node(bool isLeaf)
    : isLeaf(isLeaf) {}

BTree::BTree(int order)
    : IndexTree(order), root(nullptr) {
    if (order < 3) {
        throw std::invalid_argument("BTree order must be at least 3.");
    }
}

BTree::~BTree() {
    auto clear = [](auto&& self, Node* node) -> void {
        if (node == nullptr) { return; }
        for (Node* child : node->children) { self(self, child); }
        delete node;
    };
    clear(clear, root);
}

/*
  Main functions:
  - search(): return the rid associated with the given key, or -1 if not found.
  - insert(): insert the key-rid pair into the B-Tree.
  - remove(): remove the key and its associated rid from the B-Tree. If the key does not exist, do nothing.
*/

int BTree::search(int key) const {
    return search(root, key);
}

void BTree::insert(int key, int rid) {
    // TODO
}

void BTree::remove(int key) {
    // TODO
}

/*
  Helper functions:
  - findIndex(): return the index of the first entry whose key is equal to or greater than to the given key.
  - search(Node*): recursive helper for search().
*/

int BTree::findIndex(const std::vector<Entry>& entries, int key) const {
  int index = 0;

  while (index < static_cast<int>(entries.size()) && entries[index].key < key) {
    ++index;
  }

  return index;
}

int BTree::search(Node* node, int key) const {
  if (node == nullptr) { return -1; }
  int index = findIndex(node->entries, key);

  if (index < static_cast<int>(node->entries.size()) &&
    node->entries[index].key == key) {
    return node->entries[index].rid;
  }

  if (node->isLeaf) { return -1; }

  return search(node->children[index], key);
}