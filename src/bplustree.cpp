#include "bplustree.h"

#include <stdexcept>

BPlusTree::Entry::Entry(int key, int rid) : key(key), rid(rid) {}

BPlusTree::Node::Node(bool isLeaf) : isLeaf(isLeaf) {}

BPlusTree::InternalNode::InternalNode() : Node(false) {}

BPlusTree::LeafNode::LeafNode() : Node(true), next(nullptr) {}

BPlusTree::BPlusTree(int order) : IndexTree(order), root(nullptr) {
  if (order < 3) {
    throw std::invalid_argument("BPlusTree order must be at least 3.");
  }
}

BPlusTree::~BPlusTree() {
  auto clear = [](auto &&self, Node *node) -> void {
    if (node == nullptr) {
      return;
    }
    if (!node->isLeaf) {
      auto *internal = static_cast<InternalNode *>(node);
      for (Node *child : internal->children) {
        self(self, child);
      }
    }
    delete node;
  };
  clear(clear, root);
}

/*
  Main functions:
  - search(): return the rid associated with the given key, or -1 if not found.
  - insert(): insert the key-rid pair into the B+-Tree.
  - remove(): remove the key and its associated rid from the B+-Tree. If the key
  does not exist, do nothing.
*/

int BPlusTree::search(int key) const { return search(root, key); }

void BPlusTree::insert(int key, int rid) {
  Entry newEntry(key, rid);

  if (root == nullptr) {
    auto *leaf = new LeafNode();
    leaf->entries.push_back(newEntry);
    root = leaf;
    return;
  }

  std::vector<std::pair<InternalNode *, int>> path;
  Node *current = root;

  while (!current->isLeaf) {
    auto *internal = static_cast<InternalNode *>(current);
    int index = findIndex(internal->keys, key);

    if (index < static_cast<int>(internal->keys.size()) &&
        internal->keys[index] == key) {
      ++index;
    }

    path.push_back({internal, index});
    current = internal->children[index];
  }

  auto *leaf = static_cast<LeafNode *>(current);
  int index = findIndex(leaf->entries, key);

  // If the key already exists in a leaf node, do not insert.
  if (index < static_cast<int>(leaf->entries.size()) &&
      leaf->entries[index].key == key) {
    return;
  }

  leaf->entries.insert(leaf->entries.begin() + index, newEntry);
  handleOverflow(leaf, path);
}

void BPlusTree::remove(int key) {
  // TODO
}

/*
  Helper functions:
  - findIndex(): return the index of the first key/entry whose key is equal to
  or greater than the given key.
  - search(): recursive helper for search().
  - splitNode(): split the given node and return the promoted separator key.
  - handleOverflow(): check if the node has overflowed.
  - concatenation(): concatenate the child at childIndex with its sibling.
  - redistribution(): redistribute entries between the child at childIndex and
  its sibling.
  - handleUnderflow(): check if the node has underflowed.
*/

int BPlusTree::findIndex(const std::vector<int> &keys, int key) const {
  int index = 0;

  while (index < static_cast<int>(keys.size()) && keys[index] < key) {
    ++index;
  }

  return index;
}

int BPlusTree::findIndex(const std::vector<Entry> &entries, int key) const {
  int index = 0;

  while (index < static_cast<int>(entries.size()) && entries[index].key < key) {
    ++index;
  }

  return index;
}

int BPlusTree::search(Node *node, int key) const {
  if (node == nullptr) {
    return -1;
  }

  if (node->isLeaf) { // It must reach to a leaf node.
    auto *leaf = static_cast<LeafNode *>(node);
    int index = findIndex(leaf->entries, key);

    if (index < static_cast<int>(leaf->entries.size()) &&
        leaf->entries[index].key == key) {
      return leaf->entries[index].rid;
    }

    return -1;
  }

  auto *internal = static_cast<InternalNode *>(node);
  int index = findIndex(internal->keys, key);

  if (index < static_cast<int>(internal->keys.size()) &&
      internal->keys[index] == key) {
    ++index;
  }

  return search(internal->children[index], key);
}

int BPlusTree::splitNode(Node *node, Node *&rightNode) {
  if (node->isLeaf) {
    auto *leaf = static_cast<LeafNode *>(node);
    auto *rightLeaf = new LeafNode();
    int mid = static_cast<int>(leaf->entries.size()) / 2;

    rightLeaf->entries.assign(leaf->entries.begin() + mid, leaf->entries.end());
    leaf->entries.erase(leaf->entries.begin() + mid, leaf->entries.end());

    rightLeaf->next = leaf->next;
    leaf->next = rightLeaf;
    rightNode = rightLeaf;

    splitCount++;
    return rightLeaf->entries.front().key; // copy up! not move up
  }

  auto *internal = static_cast<InternalNode *>(node);
  auto *rightInternal = new InternalNode();
  int mid = static_cast<int>(internal->keys.size()) / 2;
  int upKey = internal->keys[mid];

  rightInternal->keys.assign(internal->keys.begin() + mid + 1,
                             internal->keys.end());
  rightInternal->children.assign(internal->children.begin() + mid + 1,
                                 internal->children.end());

  internal->keys.erase(internal->keys.begin() + mid, internal->keys.end());
  internal->children.resize(mid + 1);
  rightNode = rightInternal;

  splitCount++;
  return upKey;
}

void BPlusTree::handleOverflow(
    Node *node, std::vector<std::pair<InternalNode *, int>> &path) {
  auto entryCount = [](Node *current) {
    if (current->isLeaf) {
      return static_cast<int>(static_cast<LeafNode *>(current)->entries.size());
    }

    return static_cast<int>(static_cast<InternalNode *>(current)->keys.size());
  };

  while (entryCount(node) > maxEntries()) {
    Node *rightNode = nullptr;
    int upKey = splitNode(node, rightNode);

    if (path.empty()) {
      auto *newRoot = new InternalNode();
      newRoot->keys.push_back(upKey);
      newRoot->children.push_back(node);
      newRoot->children.push_back(rightNode);
      root = newRoot;
      return;
    }

    auto [parent, childIndex] = path.back();
    path.pop_back();

    parent->keys.insert(parent->keys.begin() + childIndex, upKey);
    parent->children.insert(parent->children.begin() + childIndex + 1,
                            rightNode);
    node = parent;
  }
}

void BPlusTree::concatenation(InternalNode *parent, int leftIndex) {
  // TODO
}

void BPlusTree::redistribution(InternalNode *parent, int leftIndex) {
  // TODO
}

void BPlusTree::handleUnderflow(
    Node *node, std::vector<std::pair<InternalNode *, int>> &path) {
  // TODO
}
