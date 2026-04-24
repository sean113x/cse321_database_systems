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
  - insert(): insert the key-rid pair into the B-Tree. If the key already exists, update the rid.
  - remove(): remove the key and its associated rid from the B-Tree. If the key does not exist, do nothing.
*/

int BTree::search(int key) const {
    return search(root, key);
}

void BTree::insert(int key, int rid) {
    // TODO: implement insertion.
    (void)key;
    (void)rid;
}

void BTree::remove(int key) {
    // TODO: implement deletion.
    (void)key;
}

