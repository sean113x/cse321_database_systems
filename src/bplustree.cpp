#include "bplustree.h"

#include <stdexcept>

BPlusTree::Entry::Entry(int key, int rid)
    : key(key), rid(rid) {}

BPlusTree::Node::Node(bool isLeaf)
    : isLeaf(isLeaf) {}

BPlusTree::InternalNode::InternalNode()
    : Node(false) {}

BPlusTree::LeafNode::LeafNode()
    : Node(true), next(nullptr) {}

BPlusTree::BPlusTree(int order)
    : IndexTree(order), root(nullptr) {
  if (order < 3) {
    throw std::invalid_argument("BPlusTree order must be at least 3.");
  }
}

BPlusTree::~BPlusTree() {
  auto clear = [](auto&& self, Node* node) -> void {
    if (node == nullptr) { return; }
    if (!node->isLeaf) {
      auto* internal = static_cast<InternalNode*>(node);
      for (Node* child : internal->children) { self(self, child); }
    }
    delete node;
  };
  clear(clear, root);
}

/*
  Main functions:
  - search(): return the rid associated with the given key, or -1 if not found.
  - insert(): insert the key-rid pair into the B+-Tree.
  - remove(): remove the key and its associated rid from the B+-Tree. If the key does not exist, do nothing.
*/

int BPlusTree::search(int key) const {
  return search(root, key);
}

void BPlusTree::insert(int key, int rid) {
  // TODO
}

void BPlusTree::remove(int key) {
  // TODO
}

/*
  Helper functions:
  - findIndex(): return the index of the first key/entry whose key is equal to or greater than the given key.
  - search(): recursive helper for search().
  - splitNode(): split the given node and return the promoted separator key.
  - handleOverflow(): check if the node has overflowed.
  - concatenation(): concatenate the child at childIndex with its sibling.
  - redistribution(): redistribute entries between the child at childIndex and its sibling.
  - handleUnderflow(): check if the node has underflowed.
*/

int BPlusTree::findIndex(const std::vector<int>& keys, int key) const {
  int index = 0;

  while (index < static_cast<int>(keys.size()) && keys[index] < key) {
    ++index;
  }

  return index;
}

int BPlusTree::findIndex(const std::vector<Entry>& entries, int key) const {
  int index = 0;

  while (index < static_cast<int>(entries.size()) && entries[index].key < key) {
    ++index;
  }

  return index;
}

int BPlusTree::search(Node* node, int key) const {
  if (node == nullptr) { return -1; }

  if (node->isLeaf) { // It must reach to a leaf node.
    auto* leaf = static_cast<LeafNode*>(node);
    int index = findIndex(leaf->entries, key);

    if (index < static_cast<int>(leaf->entries.size()) &&
        leaf->entries[index].key == key) {
      return leaf->entries[index].rid;
    }

    return -1;
  }

  auto* internal = static_cast<InternalNode*>(node);
  int index = findIndex(internal->keys, key);

  if (index < static_cast<int>(internal->keys.size()) &&
      internal->keys[index] == key) {
    ++index;
  }

  return search(internal->children[index], key);
}

int BPlusTree::splitNode(Node* node, Node*& rightNode) {
  // TODO
}

void BPlusTree::handleOverflow(Node* node, std::vector<std::pair<InternalNode*, int>>& path) {
  // TODO
}

void BPlusTree::concatenation(InternalNode* parent, int leftIndex) {
  // TODO
}

void BPlusTree::redistribution(InternalNode* parent, int leftIndex) {
  // TODO
}

void BPlusTree::handleUnderflow(Node* node, std::vector<std::pair<InternalNode*, int>>& path) {
  // TODO
}
