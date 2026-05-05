#include "opt_bstar.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

OptBStarTree::Entry::Entry(int key, int rid) : key(key), rid(rid) {}

OptBStarTree::Node::Node(bool isLeaf) : isLeaf(isLeaf) {}

OptBStarTree::Options::Options()
    : nodeSearchPolicy(NodeSearchPolicy::Linear), enableHotKeyCache(false),
      hotKeyCacheCapacity(4),
      overflowPolicy(OverflowPolicy::EagerRedistribution),
      selectiveRedistributionAlpha(0.25) {}

OptBStarTree::OptBStarTree(int order) : OptBStarTree(order, Options()) {}

OptBStarTree::OptBStarTree(int order, Options options)
    : IndexTree(order), root(nullptr), options(options) {
  if (order < 3) {
    throw std::invalid_argument("OptBStarTree order must be at least 3.");
  }
  if (this->options.hotKeyCacheCapacity < 0) {
    throw std::invalid_argument("Hot-key cache capacity cannot be negative.");
  }
  if (this->options.selectiveRedistributionAlpha < 0.0) {
    throw std::invalid_argument(
        "Selective redistribution alpha cannot be negative.");
  }
}

OptBStarTree::~OptBStarTree() {
  auto clear = [](auto &&self, Node *node) -> void {
    if (node == nullptr) {
      return;
    }
    for (Node *child : node->children) {
      self(self, child);
    }
    delete node;
  };
  clear(clear, root);
}

int OptBStarTree::search(int key) const { return search(root, key); }

int OptBStarTree::calculateHeight() const {
  int height = 0;
  Node *current = root;

  while (current != nullptr) {
    height++;

    if (current->isLeaf || current->children.empty()) {
      break;
    }

    current = current->children[0];
  }

  return height;
}

std::vector<int> OptBStarTree::range_query(int startKey, int endKey) const {
  std::vector<int> rids;

  if (startKey > endKey) {
    return rids;
  }

  range_query(root, startKey, endKey, rids);
  return rids;
}

void OptBStarTree::insert(int key, int rid) {
  Entry newEntry(key, rid);

  if (root == nullptr) {
    root = new Node(true);
    root->entries.push_back(newEntry);
    numNode++;
    numEntry++;
    return;
  }

  std::vector<std::pair<Node *, int>> path;
  Node *current = root;
  countNodeRead();

  while (!current->isLeaf) {
    int index = findIndex(current, key);

    if (index < static_cast<int>(current->entries.size()) &&
        current->entries[index].key == key) {
      return;
    }
    path.push_back({current, index});
    current = current->children[index];
    countNodeRead();
  }

  int index = findIndex(current, key);

  if (index < static_cast<int>(current->entries.size()) &&
      current->entries[index].key == key) {
    return;
  }

  current->entries.insert(current->entries.begin() + index, newEntry);
  current->hotKeys.clear();
  numEntry++;
  handleOverflow(current, path);
}

void OptBStarTree::remove(int key) {
  if (root == nullptr) {
    return;
  }

  std::vector<std::pair<Node *, int>> path;
  Node *current = root;
  countNodeRead();

  while (true) {
    int index = findIndex(current, key);

    if (index < static_cast<int>(current->entries.size()) &&
        current->entries[index].key == key) {
      if (!current->isLeaf) {
        Node *successor = current->children[index + 1];
        path.push_back({current, index + 1});
        countNodeRead();

        while (!successor->isLeaf) {
          path.push_back({successor, 0});
          successor = successor->children[0];
          countNodeRead();
        }

        current->entries[index] = successor->entries.front();
        current->hotKeys.clear();
        current = successor;
        index = 0;
      }

      current->entries.erase(current->entries.begin() + index);
      current->hotKeys.clear();
      numEntry--;
      handleUnderflow(current, path);
      return;
    }

    if (current->isLeaf) {
      return;
    }

    path.push_back({current, index});
    current = current->children[index];
    countNodeRead();
  }
}

double OptBStarTree::getNodeUtilization() const {
  if (numNode == 0) {
    return 0.0;
  }

  int totalCapacity = rootMaxEntries() + (numNode - 1) * maxEntries();
  return 100.0 * numEntry / totalCapacity;
}

void OptBStarTree::resetMetrics(bool clearHotKeyCache) const {
  metrics = Metrics{};
  if (clearHotKeyCache) {
    clearHotKeyCaches(root);
  }
}

int OptBStarTree::rootMaxEntries() const {
  return 2 * minEntries();
}

int OptBStarTree::nodeMaxEntries(const Node *node) const {
  return node == root ? rootMaxEntries() : maxEntries();
}

void OptBStarTree::clearHotKeyCaches(Node *node) const {
  if (node == nullptr) {
    return;
  }

  if (options.enableHotKeyCache && options.hotKeyCacheCapacity > 0) {
    node->hotKeys.assign(static_cast<std::size_t>(options.hotKeyCacheCapacity),
                         HotKey{});
  } else {
    node->hotKeys.clear();
  }
  for (Node *child : node->children) {
    clearHotKeyCaches(child);
  }
}

std::size_t OptBStarTree::hotKeyCacheSlot(int key) const {
  std::uint32_t hash = static_cast<std::uint32_t>(key);
  hash *= 2654435761u;

  std::size_t capacity = static_cast<std::size_t>(options.hotKeyCacheCapacity);
  if ((capacity & (capacity - 1)) == 0) {
    return static_cast<std::size_t>(hash) & (capacity - 1);
  }
  return static_cast<std::size_t>(hash) % capacity;
}

bool OptBStarTree::findInHotKeyCache(const Node *node, int key,
                                     int &index) const {
  if (!options.enableHotKeyCache || options.hotKeyCacheCapacity == 0) {
    return false;
  }

  if (node->hotKeys.empty()) {
    metrics.hotKeyCacheMisses++;
    return false;
  }

  const HotKey &hotKey = node->hotKeys[hotKeyCacheSlot(key)];
  if (hotKey.valid && hotKey.key == key) {
    index = hotKey.index;
    metrics.hotKeyCacheHits++;
    return true;
  }

  metrics.hotKeyCacheMisses++;
  return false;
}

void OptBStarTree::rememberHotKey(const Node *node, int key, int index) const {
  if (!options.enableHotKeyCache || options.hotKeyCacheCapacity == 0) {
    return;
  }

  std::size_t capacity = static_cast<std::size_t>(options.hotKeyCacheCapacity);
  if (node->hotKeys.size() != capacity) {
    node->hotKeys.assign(capacity, HotKey{});
  }

  node->hotKeys[hotKeyCacheSlot(key)] = {key, index, true};
}

int OptBStarTree::findIndex(const Node *node, int key) const {
  metrics.intraNodeSearchCount++;

  int index = 0;
  if (findInHotKeyCache(node, key, index)) {
    return index;
  }

  if (options.nodeSearchPolicy == NodeSearchPolicy::Binary) {
    index = findIndexBinary(node->entries, key);
  } else {
    index = findIndexLinear(node->entries, key);
  }

  rememberHotKey(node, key, index);
  return index;
}

int OptBStarTree::findIndexLinear(const std::vector<Entry> &entries,
                                  int key) const {
  int index = 0;

  while (index < static_cast<int>(entries.size())) {
    metrics.intraNodeKeyComparisons++;
    if (!(entries[index].key < key)) {
      break;
    }
    ++index;
  }

  return index;
}

int OptBStarTree::findIndexBinary(const std::vector<Entry> &entries,
                                  int key) const {
  int first = 0;
  int count = static_cast<int>(entries.size());

  while (count > 0) {
    int step = count / 2;
    int mid = first + step;

    metrics.intraNodeKeyComparisons++;
    if (entries[mid].key < key) {
      first = mid + 1;
      count -= step + 1;
    } else {
      count = step;
    }
  }

  return first;
}

int OptBStarTree::search(Node *node, int key) const {
  if (node == nullptr) {
    return -1;
  }

  countNodeRead();

  int index = findIndex(node, key);

  if (index < static_cast<int>(node->entries.size()) &&
      node->entries[index].key == key) {
    return node->entries[index].rid;
  }

  if (node->isLeaf) {
    return -1;
  }

  return search(node->children[index], key);
}

void OptBStarTree::range_query(Node *node, int startKey, int endKey,
                               std::vector<int> &rids) const {
  if (node == nullptr) {
    return;
  }

  countNodeRead();

  int index = findIndex(node, startKey);

  if (!node->isLeaf) {
    range_query(node->children[index], startKey, endKey, rids);
  }

  while (index < static_cast<int>(node->entries.size()) &&
         node->entries[index].key <= endKey) {
    rids.push_back(node->entries[index].rid);
    index++;

    if (!node->isLeaf) {
      put_values(node->children[index], endKey, rids);
    }
  }
}

void OptBStarTree::put_values(Node *node, int endKey,
                              std::vector<int> &rids) const {
  if (node == nullptr) {
    return;
  }

  countNodeRead();

  for (int i = 0; i < static_cast<int>(node->entries.size()); i++) {
    if (!node->isLeaf) {
      put_values(node->children[i], endKey, rids);
    }

    if (node->entries[i].key > endKey) {
      return;
    }

    rids.push_back(node->entries[i].rid);
  }

  if (!node->isLeaf) {
    put_values(node->children.back(), endKey, rids);
  }
}

OptBStarTree::Entry OptBStarTree::splitRootNode(Node *node,
                                                Node *&rightNode) {
  int entryCount = static_cast<int>(node->entries.size());
  int mid = entryCount / 2;

  Entry upEntry = node->entries[mid];
  rightNode = new Node(node->isLeaf);
  numNode++;

  rightNode->entries.assign(node->entries.begin() + mid + 1,
                            node->entries.end());
  node->entries.erase(node->entries.begin() + mid, node->entries.end());

  if (!node->isLeaf) {
    rightNode->children.assign(node->children.begin() + mid + 1,
                               node->children.end());
    node->children.resize(mid + 1);
  }

  node->hotKeys.clear();
  rightNode->hotKeys.clear();
  splitCount++;
  return upEntry;
}

void OptBStarTree::splitNodeTwoToThree(Node *parent, int leftIndex) {
  Node *left = parent->children[leftIndex];
  Node *right = parent->children[leftIndex + 1];
  Node *middle = new Node(left->isLeaf);
  numNode++;

  std::vector<Entry> entries = left->entries;
  entries.push_back(parent->entries[leftIndex]);
  entries.insert(entries.end(), right->entries.begin(), right->entries.end());

  int entryCount = static_cast<int>(entries.size()) - 2;
  int firstUpIndex = (entryCount + 2) / 3;
  int secondUpIndex = firstUpIndex + 1 + (entryCount + 1) / 3;

  left->entries.assign(entries.begin(), entries.begin() + firstUpIndex);
  parent->entries[leftIndex] = entries[firstUpIndex];
  middle->entries.assign(entries.begin() + firstUpIndex + 1,
                         entries.begin() + secondUpIndex);
  parent->entries.insert(parent->entries.begin() + leftIndex + 1,
                         entries[secondUpIndex]);
  right->entries.assign(entries.begin() + secondUpIndex + 1, entries.end());

  if (!left->isLeaf) {
    std::vector<Node *> children = left->children;
    children.insert(children.end(), right->children.begin(),
                    right->children.end());

    int firstChildEnd = firstUpIndex + 1;
    int secondChildEnd = secondUpIndex + 1;

    left->children.assign(children.begin(), children.begin() + firstChildEnd);
    middle->children.assign(children.begin() + firstChildEnd,
                            children.begin() + secondChildEnd);
    right->children.assign(children.begin() + secondChildEnd, children.end());
  }

  parent->children.insert(parent->children.begin() + leftIndex + 1, middle);
  parent->hotKeys.clear();
  left->hotKeys.clear();
  middle->hotKeys.clear();
  right->hotKeys.clear();
  splitCount++;
  metrics.twoToThreeSplitCount++;
}

int OptBStarTree::estimateRedistributionMoves(const Node *left,
                                              const Node *right) const {
  int oldLeftCount = static_cast<int>(left->entries.size());
  int oldRightCount = static_cast<int>(right->entries.size());
  int totalCount = oldLeftCount + 1 + oldRightCount;
  int newLeftCount = totalCount / 2;
  int newRightCount = totalCount - newLeftCount - 1;

  return std::abs(newLeftCount - oldLeftCount) +
         std::abs(newRightCount - oldRightCount);
}

OptBStarTree::RedistributionCandidate
OptBStarTree::chooseRedistributionCandidate(Node *parent,
                                            int childIndex) const {
  if (childIndex < static_cast<int>(parent->children.size()) - 1 &&
      static_cast<int>(parent->children[childIndex + 1]->entries.size()) <
          maxEntries()) {
    Node *left = parent->children[childIndex];
    Node *right = parent->children[childIndex + 1];
    return {true, childIndex, estimateRedistributionMoves(left, right)};
  }

  if (childIndex > 0 &&
      static_cast<int>(parent->children[childIndex - 1]->entries.size()) <
          maxEntries()) {
    Node *left = parent->children[childIndex - 1];
    Node *right = parent->children[childIndex];
    return {true, childIndex - 1, estimateRedistributionMoves(left, right)};
  }

  return {};
}

bool OptBStarTree::canSplitTwoToThree(Node *parent, int leftIndex) const {
  if (leftIndex < 0 ||
      leftIndex + 1 >= static_cast<int>(parent->children.size())) {
    return false;
  }

  Node *left = parent->children[leftIndex];
  Node *right = parent->children[leftIndex + 1];
  int childEntryCount = static_cast<int>(left->entries.size()) +
                        static_cast<int>(right->entries.size()) - 1;

  return childEntryCount >= 3 * minEntries() &&
         childEntryCount <= 3 * maxEntries();
}

int OptBStarTree::chooseTwoToThreeSplitLeftIndex(Node *parent,
                                                 int childIndex) const {
  if (canSplitTwoToThree(parent, childIndex)) {
    return childIndex;
  }
  if (canSplitTwoToThree(parent, childIndex - 1)) {
    return childIndex - 1;
  }
  return -1;
}

bool OptBStarTree::shouldRedistribute(
    const RedistributionCandidate &candidate) const {
  if (!candidate.exists) {
    return false;
  }

  if (options.overflowPolicy == OverflowPolicy::EagerRedistribution) {
    return true;
  }

  double threshold =
      options.selectiveRedistributionAlpha * static_cast<double>(maxEntries());
  return static_cast<double>(candidate.estimatedMovedEntries) <= threshold;
}

void OptBStarTree::redistributeOverflow(
    Node *parent, const RedistributionCandidate &candidate) {
  Node *left = parent->children[candidate.leftIndex];
  Node *right = parent->children[candidate.leftIndex + 1];

  std::vector<Entry> entries = left->entries;
  entries.push_back(parent->entries[candidate.leftIndex]);
  entries.insert(entries.end(), right->entries.begin(), right->entries.end());

  int mid = static_cast<int>(entries.size()) / 2;

  left->entries.assign(entries.begin(), entries.begin() + mid);
  parent->entries[candidate.leftIndex] = entries[mid];
  right->entries.assign(entries.begin() + mid + 1, entries.end());

  if (!left->isLeaf) {
    std::vector<Node *> children = left->children;
    children.insert(children.end(), right->children.begin(),
                    right->children.end());

    int leftChildCount = static_cast<int>(left->entries.size()) + 1;

    left->children.assign(children.begin(), children.begin() + leftChildCount);
    right->children.assign(children.begin() + leftChildCount, children.end());
  }

  parent->hotKeys.clear();
  left->hotKeys.clear();
  right->hotKeys.clear();
  metrics.redistributionCount++;
  metrics.redistributionMovedEntries += candidate.estimatedMovedEntries;
}

void OptBStarTree::splitRoot(Node *node) {
  Node *rightNode = nullptr;
  Entry upEntry = splitRootNode(node, rightNode);

  root = new Node(false);
  numNode++;
  root->entries = {upEntry};
  root->children = {node, rightNode};
  metrics.rootSplitCount++;
}

void OptBStarTree::handleOverflow(
    Node *node, std::vector<std::pair<Node *, int>> &path) {
  while (static_cast<int>(node->entries.size()) > nodeMaxEntries(node)) {
    if (path.empty()) {
      splitRoot(node);
      return;
    }

    auto [parent, childIndex] = path.back();
    path.pop_back();

    RedistributionCandidate candidate =
        chooseRedistributionCandidate(parent, childIndex);

    if (options.overflowPolicy == OverflowPolicy::EagerRedistribution &&
        candidate.exists) {
      redistributeOverflow(parent, candidate);
      return;
    }

    if (options.overflowPolicy == OverflowPolicy::SelectiveRedistribution &&
        candidate.exists && shouldRedistribute(candidate)) {
      redistributeOverflow(parent, candidate);
      return;
    }

    int leftIndex = chooseTwoToThreeSplitLeftIndex(parent, childIndex);
    if (leftIndex != -1) {
      if (candidate.exists) {
        metrics.skippedRedistributionCount++;
      }
      splitNodeTwoToThree(parent, leftIndex);
      node = parent;
      continue;
    }

    if (candidate.exists) {
      metrics.forcedRedistributionCount++;
      redistributeOverflow(parent, candidate);
      return;
    }

    throw std::logic_error("B*-tree overflow cannot be handled safely.");
  }
}

void OptBStarTree::concatenation(Node *parent, int childIndex) {
  int startIndex = -1;
  int childCount = static_cast<int>(parent->children.size());

  for (int candidate : {childIndex - 2, childIndex - 1, childIndex}) {
    if (candidate < 0 || candidate + 2 >= childCount) {
      continue;
    }

    Node *first = parent->children[candidate];
    Node *second = parent->children[candidate + 1];
    Node *third = parent->children[candidate + 2];

    int total = static_cast<int>(first->entries.size()) +
                static_cast<int>(second->entries.size()) +
                static_cast<int>(third->entries.size()) + 2;

    int childEntryCount = total - 1;
    if (childEntryCount >= 2 * minEntries() &&
        childEntryCount <= 2 * maxEntries()) {
      startIndex = candidate;
      break;
    }
  }

  if (startIndex == -1) {
    int leftIndex = (childIndex > 0) ? childIndex - 1 : childIndex;
    Node *left = parent->children[leftIndex];
    Node *right = parent->children[leftIndex + 1];

    left->entries.push_back(parent->entries[leftIndex]);
    left->entries.insert(left->entries.end(), right->entries.begin(),
                         right->entries.end());

    if (!left->isLeaf) {
      left->children.insert(left->children.end(), right->children.begin(),
                            right->children.end());
    }

    parent->entries.erase(parent->entries.begin() + leftIndex);
    parent->children.erase(parent->children.begin() + leftIndex + 1);
    parent->hotKeys.clear();
    left->hotKeys.clear();
    delete right;
    numNode--;
    return;
  }

  Node *first = parent->children[startIndex];
  Node *second = parent->children[startIndex + 1];
  Node *third = parent->children[startIndex + 2];

  std::vector<Entry> entries = first->entries;
  entries.push_back(parent->entries[startIndex]);
  entries.insert(entries.end(), second->entries.begin(), second->entries.end());
  entries.push_back(parent->entries[startIndex + 1]);
  entries.insert(entries.end(), third->entries.begin(), third->entries.end());

  int childEntryCount = static_cast<int>(entries.size()) - 1;
  int leftEntryCount = (childEntryCount + 1) / 2;
  if (leftEntryCount > maxEntries()) {
    leftEntryCount = maxEntries();
  }
  if (childEntryCount - leftEntryCount < minEntries()) {
    leftEntryCount = childEntryCount - minEntries();
  }

  first->entries.assign(entries.begin(), entries.begin() + leftEntryCount);
  parent->entries[startIndex] = entries[leftEntryCount];
  second->entries.assign(entries.begin() + leftEntryCount + 1, entries.end());

  if (!first->isLeaf) {
    std::vector<Node *> children = first->children;
    children.insert(children.end(), second->children.begin(),
                    second->children.end());
    children.insert(children.end(), third->children.begin(),
                    third->children.end());

    int firstChildCount = static_cast<int>(first->entries.size()) + 1;

    first->children.assign(children.begin(),
                           children.begin() + firstChildCount);
    second->children.assign(children.begin() + firstChildCount, children.end());
  }

  parent->entries.erase(parent->entries.begin() + startIndex + 1);
  parent->children.erase(parent->children.begin() + startIndex + 2);
  parent->hotKeys.clear();
  first->hotKeys.clear();
  second->hotKeys.clear();
  delete third;
  numNode--;
}

bool OptBStarTree::redistribution2(Node *parent, int childIndex) {
  int leftIndex = -1;

  for (int candidate : {childIndex - 1, childIndex}) {
    if (candidate < 0 ||
        candidate + 1 >= static_cast<int>(parent->children.size())) {
      continue;
    }

    Node *left = parent->children[candidate];
    Node *right = parent->children[candidate + 1];
    int total = static_cast<int>(left->entries.size()) + 1 +
                static_cast<int>(right->entries.size());

    if (total >= 2 * minEntries() + 1) {
      leftIndex = candidate;
      break;
    }
  }

  if (leftIndex == -1) {
    return false;
  }

  Node *left = parent->children[leftIndex];
  Node *right = parent->children[leftIndex + 1];

  std::vector<Entry> entries = left->entries;
  entries.push_back(parent->entries[leftIndex]);
  entries.insert(entries.end(), right->entries.begin(), right->entries.end());

  int mid = static_cast<int>(entries.size()) / 2;

  left->entries.assign(entries.begin(), entries.begin() + mid);
  parent->entries[leftIndex] = entries[mid];
  right->entries.assign(entries.begin() + mid + 1, entries.end());

  if (!left->isLeaf) {
    std::vector<Node *> children = left->children;
    children.insert(children.end(), right->children.begin(),
                    right->children.end());

    int leftChildCount = static_cast<int>(left->entries.size()) + 1;

    left->children.assign(children.begin(), children.begin() + leftChildCount);
    right->children.assign(children.begin() + leftChildCount, children.end());
  }

  parent->hotKeys.clear();
  left->hotKeys.clear();
  right->hotKeys.clear();
  return true;
}

bool OptBStarTree::redistribution3(Node *parent, int childIndex) {
  int startIndex = -1;
  int childCount = static_cast<int>(parent->children.size());

  for (int candidate : {childIndex - 2, childIndex - 1, childIndex}) {
    if (candidate < 0 || candidate + 2 >= childCount) {
      continue;
    }

    Node *first = parent->children[candidate];
    Node *second = parent->children[candidate + 1];
    Node *third = parent->children[candidate + 2];

    int total = static_cast<int>(first->entries.size()) +
                static_cast<int>(second->entries.size()) +
                static_cast<int>(third->entries.size()) + 2;

    int childEntryCount = total - 2;
    if (childEntryCount >= 3 * minEntries() &&
        childEntryCount <= 3 * maxEntries()) {
      startIndex = candidate;
      break;
    }
  }

  if (startIndex == -1) {
    return false;
  }

  Node *first = parent->children[startIndex];
  Node *second = parent->children[startIndex + 1];
  Node *third = parent->children[startIndex + 2];

  std::vector<Entry> entries = first->entries;
  entries.push_back(parent->entries[startIndex]);
  entries.insert(entries.end(), second->entries.begin(), second->entries.end());
  entries.push_back(parent->entries[startIndex + 1]);
  entries.insert(entries.end(), third->entries.begin(), third->entries.end());

  int childEntryCount = static_cast<int>(entries.size()) - 2;
  int firstEntryCount = (childEntryCount + 2) / 3;
  int secondEntryCount = (childEntryCount + 1) / 3;
  int firstUpIndex = firstEntryCount;
  int secondUpIndex = firstUpIndex + 1 + secondEntryCount;

  first->entries.assign(entries.begin(), entries.begin() + firstUpIndex);
  parent->entries[startIndex] = entries[firstUpIndex];
  second->entries.assign(entries.begin() + firstUpIndex + 1,
                         entries.begin() + secondUpIndex);
  parent->entries[startIndex + 1] = entries[secondUpIndex];
  third->entries.assign(entries.begin() + secondUpIndex + 1, entries.end());

  if (!first->isLeaf) {
    std::vector<Node *> children = first->children;
    children.insert(children.end(), second->children.begin(),
                    second->children.end());
    children.insert(children.end(), third->children.begin(),
                    third->children.end());

    int firstChildEnd = firstUpIndex + 1;
    int secondChildEnd = secondUpIndex + 1;

    first->children.assign(children.begin(), children.begin() + firstChildEnd);
    second->children.assign(children.begin() + firstChildEnd,
                            children.begin() + secondChildEnd);
    third->children.assign(children.begin() + secondChildEnd, children.end());
  }

  parent->hotKeys.clear();
  first->hotKeys.clear();
  second->hotKeys.clear();
  third->hotKeys.clear();
  return true;
}

void OptBStarTree::handleUnderflow(
    Node *node, std::vector<std::pair<Node *, int>> &path) {
  while (node != root &&
         static_cast<int>(node->entries.size()) < minEntries()) {
    auto [parent, childIndex] = path.back();
    path.pop_back();

    countNodeRead();

    if (redistribution2(parent, childIndex)) {
      return;
    }
    if (redistribution3(parent, childIndex)) {
      return;
    }

    concatenation(parent, childIndex);
    node = parent;
  }

  if (root != nullptr && root->entries.empty()) {
    Node *oldRoot = root;

    if (root->isLeaf) {
      root = nullptr;
    } else {
      root = root->children[0];
    }

    delete oldRoot;
    numNode--;
  }
}
