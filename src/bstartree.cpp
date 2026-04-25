#include "bstartree.h"

#include <stdexcept>

BStarTree::Entry::Entry(int key, int rid)
    : key(key), rid(rid) {}

BStarTree::Node::Node(bool isLeaf)
    : isLeaf(isLeaf) {}

BStarTree::BStarTree(int order)
    : IndexTree(order), root(nullptr) {
    if (order < 3) {
        throw std::invalid_argument("BStarTree order must be at least 3.");
    }
}

BStarTree::~BStarTree() {
    auto clear = [](auto&& self, Node* node) -> void {
        if (node == nullptr) { return; }
        for (Node* child : node->children) { self(self, child); }
        delete node;
    };
    clear(clear, root);
}

/*
  Main functions: (same as BTree)
  - search(): return the rid associated with the given key, or -1 if not found.
  - insert(): insert the key-rid pair into the B*-Tree.
  - remove(): remove the key and its associated rid from the B*-Tree. If the key does not exist, do nothing.
*/

int BStarTree::search(int key) const {
    return search(root, key);
}

void BStarTree::insert(int key, int rid) {
  Entry newEntry(key, rid);

  if (root == nullptr) {
    root = new Node(true);
    root->entries.push_back(newEntry);
    return;
  }

  std::vector<std::pair<Node*, int>> path;
  Node* current = root;

  while (!current->isLeaf) {
    int index = findIndex(current->entries, key);

    // If the key already exists in an internal node, do not insert
    if (index < static_cast<int>(current->entries.size()) &&
        current->entries[index].key == key) { return; }
    path.push_back({current, index});
    current = current->children[index];
  }

  int index = findIndex(current->entries, key);

  // If the key already exists in a leaf node, do not insert
  if (index < static_cast<int>(current->entries.size()) &&
      current->entries[index].key == key) { return; }

  current->entries.insert(current->entries.begin() + index, newEntry);
  handleOverflow(current, path);
}

void BStarTree::remove(int key) {
  if (root == nullptr) { return; }

  std::vector<std::pair<Node*, int>> path;
  Node* current = root;

  while (true) {
    int index = findIndex(current->entries, key);

    if (index < static_cast<int>(current->entries.size()) &&
        current->entries[index].key == key) {
      if (!current->isLeaf) {
        Node* successor = current->children[index + 1];
        path.push_back({current, index + 1});

        while (!successor->isLeaf) {
          path.push_back({successor, 0});
          successor = successor->children[0];
        }

        current->entries[index] = successor->entries.front();
        current = successor;
        index = 0;
      }

      current->entries.erase(current->entries.begin() + index);
      handleUnderflow(current, path);
      return;
    }

    if (current->isLeaf) { return; }

    path.push_back({current, index});
    current = current->children[index];
  }
}

/*
  Helper functions:
  - findIndex(): return the index of the first entry whose key is equal to or greater than to the given key.
  - search(): recursive helper for search().
  - splitNode(): split two full sibling nodes into three nodes.
  - redistributeOverflow(): redistribute entries with a sibling before splitting.
  - handleOverflow(): check if the node has overflowed.
  - concatenation(): concatenate the child at childIndex with its sibling.
  - redistributeUnderflow(): redistribute entries between the child at childIndex and its sibling.
  - handleUnderflow(): check if the node has underfl-owed.
*/

int BStarTree::findIndex(const std::vector<Entry>& entries, int key) const { // same as B-tree
  int index = 0;

  while (index < static_cast<int>(entries.size()) && entries[index].key < key) {
    ++index;
  }

  return index;
}

int BStarTree::search(Node* node, int key) const { // same as B-tree
  if (node == nullptr) { return -1; }
  int index = findIndex(node->entries, key);

  if (index < static_cast<int>(node->entries.size()) &&
      node->entries[index].key == key) {
    return node->entries[index].rid;
  }

  if (node->isLeaf) { return -1; }

  return search(node->children[index], key);
}

void BStarTree::splitNode(Node*, int) {
  // TODO
}

void BStarTree::handleOverflow(Node*, std::vector<std::pair<Node*, int>>&) {
  // TODO
}

bool BStarTree::redistributeOverflow(Node*, int) {
  // TODO
}

void BStarTree::concatenation(Node*, int) {
  // TODO
}

void BStarTree::redistributeUnderflow(Node*, int) {
  // TODO
}

void BStarTree::handleUnderflow(Node*, std::vector<std::pair<Node*, int>>&) {
  // TODO
}
