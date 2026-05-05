#pragma once

#include "../index-tree.h"

#include <utility>
#include <vector>

class OptBStarTree : public IndexTree {
public:
  enum class NodeSearchPolicy {
    Linear,
    Binary,
  };

  enum class OverflowPolicy {
    DirectSplit,
    EagerRedistribution,
    SelectiveRedistribution,
  };

  struct Options {
    NodeSearchPolicy nodeSearchPolicy;
    bool enableHotKeyCache;
    int hotKeyCacheCapacity;
    OverflowPolicy overflowPolicy;
    double selectiveRedistributionAlpha;
    bool selectiveFallbackToDirectSplit;

    Options();
  };

  struct Metrics {
    long long intraNodeSearchCount = 0;
    long long intraNodeKeyComparisons = 0;
    long long hotKeyCacheHits = 0;
    long long hotKeyCacheMisses = 0;
    long long redistributionCount = 0;
    long long skippedRedistributionCount = 0;
    long long redistributionMovedEntries = 0;
    long long twoToThreeSplitCount = 0;
    long long directSplitCount = 0;
    long long rootSplitCount = 0;
  };

private:
  struct Entry {
    int key;
    int rid;
    Entry(int key, int rid);
  };

  struct HotKey {
    int key;
    int index;
  };

  struct Node {
    bool isLeaf;
    std::vector<Entry> entries;
    std::vector<Node *> children;
    mutable std::vector<HotKey> hotKeys;

    explicit Node(bool isLeaf);
  };

  struct RedistributionCandidate {
    bool exists = false;
    int leftIndex = -1;
    int estimatedMovedEntries = 0;
  };

  Node *root;
  Options options;
  mutable Metrics metrics;

  // capacity helper function
  int minEntries() const override { return (2 * maxEntries()) / 3; }
  int calculateHeight() const override;

  int rootMaxEntries() const;
  int nodeMaxEntries(const Node *node) const;

  // instrumentation helper functions
  void clearHotKeyCaches(Node *node) const;
  bool isHotKeyUsable(const Node *node, const HotKey &hotKey,
                      int key) const;
  bool findInHotKeyCache(const Node *node, int key, int &index) const;
  void rememberHotKey(const Node *node, int key, int index) const;

  // search() helper functions
  int findIndex(const Node *node, int key) const;
  int findIndexLinear(const std::vector<Entry> &entries, int key) const;
  int findIndexBinary(const std::vector<Entry> &entries, int key) const;
  int search(Node *node, int key) const;

  // range_query() helper functions
  void range_query(Node *node, int startKey, int endKey,
                   std::vector<int> &rids) const;
  void put_values(Node *node, int endKey, std::vector<int> &rids) const;

  // insert() helper functions
  Entry splitNodeDirect(Node *node, Node *&rightNode);
  void splitNodeTwoToThree(Node *parent, int leftIndex);
  int estimateRedistributionMoves(const Node *left, const Node *right) const;
  RedistributionCandidate chooseRedistributionCandidate(Node *parent,
                                                        int childIndex) const;
  bool shouldRedistribute(const RedistributionCandidate &candidate) const;
  bool redistributeOverflow(Node *parent, int childIndex);
  void insertDirectSplitIntoParent(Node *parent, int childIndex);
  void splitRoot(Node *node);
  void handleOverflow(Node *node, std::vector<std::pair<Node *, int>> &path);

  // remove() helper functions
  void concatenation(Node *parent, int childIndex);
  bool redistribution2(Node *parent, int childIndex);
  bool redistribution3(Node *parent, int childIndex);
  void handleUnderflow(Node *node, std::vector<std::pair<Node *, int>> &path);

public:
  explicit OptBStarTree(int order);
  OptBStarTree(int order, Options options);
  ~OptBStarTree() override;

  int search(int key) const override;
  std::vector<int> range_query(int startKey, int endKey) const override;
  void insert(int key, int rid) override;
  void remove(int key) override;
  double getNodeUtilization() const override;

  const Options &getOptions() const { return options; }
  Metrics getMetrics() const { return metrics; }
  void resetMetrics(bool clearHotKeyCache = false) const;

  long long getIntraNodeSearchCount() const {
    return metrics.intraNodeSearchCount;
  }
  long long getIntraNodeKeyComparisons() const {
    return metrics.intraNodeKeyComparisons;
  }
  long long getHotKeyCacheHits() const { return metrics.hotKeyCacheHits; }
  long long getHotKeyCacheMisses() const { return metrics.hotKeyCacheMisses; }
  long long getRedistributionCount() const {
    return metrics.redistributionCount;
  }
  long long getSkippedRedistributionCount() const {
    return metrics.skippedRedistributionCount;
  }
  long long getRedistributionMovedEntries() const {
    return metrics.redistributionMovedEntries;
  }
  long long getTwoToThreeSplitCount() const {
    return metrics.twoToThreeSplitCount;
  }
  long long getDirectSplitCount() const { return metrics.directSplitCount; }
  long long getRootSplitCount() const { return metrics.rootSplitCount; }
};
