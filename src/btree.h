#pragma once

#include "index-tree.h"
#include <vector>

class BTree : public IndexTree {
  private:
    struct Entry {
      int key;
      int rid;
      Entry(int key, int rid);
    };

    struct Node {
      bool isLeaf;
      std::vector<Entry> entries;
      std::vector<Node*> children;

      explicit Node(bool isLeaf);
    };

    Node* root;

    // search() helper functions
    int findIndex(const std::vector<Entry>& entries, int key) const;
    int search(Node* node, int key) const;

    // insert() helper functions
    void insertNonFull(Node* node, int key, int rid);
    void splitChild(Node* parent, int childIndex);

  public:
    explicit BTree(int order);
    ~BTree() override;

    int search(int key) const override;
    void insert(int key, int rid) override;
    void remove(int key) override;
};