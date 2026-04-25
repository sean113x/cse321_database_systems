#include "bplustree.h"

#include <stdexcept>

BPlusTree::Entry::Entry(int key, int rid)
    : key(key), rid(rid) {}

BPlusTree::Node::Node(bool isLeaf)
    : isLeaf(isLeaf), next(nullptr) {}

BPlusTree::BPlusTree(int order)
    : IndexTree(order), root(nullptr) {
  if (order < 3) {
    throw std::invalid_argument("BPlusTree order must be at least 3.");
  }
}

BPlusTree::~BPlusTree() {
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
  - insert(): insert the key-rid pair into the B+-Tree.
  - remove(): remove the key and its associated rid from the B+-Tree. If the key does not exist, do nothing.
*/

int BPlusTree::search(int key) const {
  // TODO
}

void BPlusTree::insert(int key, int rid) {
  // TODO
}

void BPlusTree::remove(int key) {
  // TODO
}

/*
  Helper functions:
  - findIndex(): return the index of the first entry whose key is equal to or greater than the given key.
  - search(): recursive helper for search().
  - splitNode(): split the given node and return the promoted separator entry.
  - handleOverflow(): check if the node has overflowed.
  - concatenation(): concatenate the child at childIndex with its sibling.
  - redistribution(): redistribute entries between the child at childIndex and its sibling.
  - handleUnderflow(): check if the node has underflowed.
*/

int BPlusTree::findIndex(const std::vector<Entry>& entries, int key) const {
  // TODO
}

int BPlusTree::search(Node* node, int key) const {
  // TODO
}

BPlusTree::Entry BPlusTree::splitNode(Node* node, Node*& rightNode) {
  // TODO
}

void BPlusTree::handleOverflow(Node* node, std::vector<std::pair<Node*, int>>& path) {
  // TODO
}

void BPlusTree::concatenation(Node* parent, int leftIndex) {
  // TODO
}

void BPlusTree::redistribution(Node* parent, int leftIndex) {
  // TODO
}

void BPlusTree::handleUnderflow(Node* node, std::vector<std::pair<Node*, int>>& path) {
  // TODO
}
