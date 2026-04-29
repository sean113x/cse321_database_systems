#include "experiment1.h"

#include "../dataset_handler/dataset.h"
#include "../index_tree/bplustree.h"
#include "../index_tree/bstartree.h"
#include "../index_tree/btree.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {
constexpr int warmupRuns = 10;
constexpr int minMeasuredRuns = 30;
constexpr int maxMeasuredRuns = 100;
constexpr double targetRsd = 0.01;

struct TreeSpec {
  const char *name;
  int type;
};

std::unique_ptr<IndexTree> createTree(int type, int order) {
  if (type == 1) {
    return std::make_unique<BTree>(order);
  }
  if (type == 2) {
    return std::make_unique<BStarTree>(order);
  }
  return std::make_unique<BPlusTree>(order);
}

double mean(const std::vector<double> &values) {
  double sum = 0.0;
  for (double value : values) {
    sum += value;
  }
  return sum / static_cast<double>(values.size());
}

double median(std::vector<double> values) {
  std::sort(values.begin(), values.end());
  int mid = static_cast<int>(values.size()) / 2;

  if (values.size() % 2 == 1) {
    return values[mid];
  }

  return (values[mid - 1] + values[mid]) / 2.0;
}

double stddev(const std::vector<double> &values, double avg) {
  double sum = 0.0;
  for (double value : values) {
    double diff = value - avg;
    sum += diff * diff;
  }
  return values.size() < 2
             ? 0.0
             : std::sqrt(sum / static_cast<double>(values.size() - 1));
}

long long buildTree(IndexTree &tree, const std::vector<int> &keys) {
  auto start = std::chrono::steady_clock::now(); // start timer
  for (int rid = 0; rid < static_cast<int>(keys.size()); ++rid) {
    tree.insert(keys[rid], rid);
  }
  auto end = std::chrono::steady_clock::now(); // end timer
  return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
      .count();
}

void createResultsDirectory() {
  if (mkdir("results_experiment", 0755) != 0 && errno != EEXIST) {
    throw std::runtime_error("Failed to create results_experiment directory");
  }
}
} // namespace

int runExperiment1() {
  const std::vector<TreeSpec> trees = {
      {"btree", 1}, {"bstar", 2}, {"bplus", 3}};
  const std::vector<int> orders = {3, 5, 10, 16, 32, 64, 128, 256, 512, 1024};

  Dataset dataset = loadDataset("data/student.csv");
  std::vector<int> keys;
  keys.reserve(dataset.size());
  for (int rid = 0; rid < dataset.size(); ++rid) {
    keys.push_back(dataset.getKey(rid));
  }

  createResultsDirectory();
  std::ofstream runFile("results_experiment/experiment1_runs.csv");
  std::ofstream summaryFile("results_experiment/experiment1_summary.csv");
  runFile << std::setprecision(12);
  summaryFile << std::setprecision(12);

  runFile << "tree,order,run,execution_time_ns\n";
  summaryFile << "tree,order,records,warmup_runs,measured_runs,"
              << "mean_execution_time_ns,median_execution_time_ns,"
              << "stddev_execution_time_ns,rsd,mean_execution_time_ms,"
              << "median_execution_time_ms,split_count,tree_height,"
              << "num_nodes,num_entries,node_utilization\n";

  std::cout << "Experiment 1: Insertion & Parameter Tuning\n";
  std::cout << "Records: " << keys.size() << "\n\n";

  for (const TreeSpec &spec : trees) {
    for (int order : orders) {
      for (int run = 0; run < warmupRuns; ++run) {
        auto tree = createTree(spec.type, order);
        buildTree(*tree, keys);
      }

      std::vector<double> executionTimes;
      std::unique_ptr<IndexTree> tree;
      double avg = 0.0;
      double sd = 0.0;
      double rsd = 0.0;

      for (int run = 1; run <= maxMeasuredRuns; ++run) {
        tree = createTree(spec.type, order);
        long long ns = buildTree(*tree, keys);

        executionTimes.push_back(static_cast<double>(ns));
        avg = mean(executionTimes);
        sd = stddev(executionTimes, avg);
        rsd = sd / avg;

        runFile << spec.name << ',' << order << ',' << run << ',' << ns << '\n';

        if (run >= minMeasuredRuns && rsd < targetRsd) {
          break;
        }
      }

      double meanMs = avg / 1000000.0;
      double med = median(executionTimes);
      double medianMs = med / 1000000.0;

      summaryFile << spec.name << ',' << order << ',' << keys.size() << ','
                  << warmupRuns << ',' << executionTimes.size() << ',' << avg
                  << ',' << med << ',' << sd << ',' << rsd << ',' << meanMs
                  << ',' << medianMs << ',' << tree->getSplitCount() << ','
                  << tree->getHeight() << ',' << tree->getNumNode() << ','
                  << tree->getNumEntry() << ',' << tree->getNodeUtilization()
                  << '\n';

      std::cout << spec.name << " order=" << order
                << " runs=" << executionTimes.size()
                << " median_ms=" << medianMs << " mean_ms=" << meanMs
                << " rsd=" << rsd << " splits=" << tree->getSplitCount()
                << " height=" << tree->getHeight()
                << " utilization=" << tree->getNodeUtilization() << "%\n";
    }
  }

  std::cout << "\nSaved results_experiment/experiment1_runs.csv\n";
  std::cout << "Saved results_experiment/experiment1_summary.csv\n";
  return 0;
}
