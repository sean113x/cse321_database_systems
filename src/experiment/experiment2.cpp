#include "experiment2.h"

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
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {
constexpr int queryCount = 10000;
constexpr int warmupRuns = 10;
constexpr int minMeasuredRuns = 30;
constexpr int maxRuns = 200;
constexpr int searchRepeats = 10;
constexpr double targetRsd = 0.02;

struct TreeSpec {
  const char *name;
  int type;
};

struct RunState {
  const TreeSpec *spec;
  std::unique_ptr<IndexTree> tree;
  std::vector<double> executionTimes;
  double avg = 0.0;
  double sd = 0.0;
  double rsd = 0.0;
  bool done = false;

  explicit RunState(const TreeSpec *spec) : spec(spec) {}
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
  return values.size() % 2 == 1 ? values[mid]
                                : (values[mid - 1] + values[mid]) / 2.0;
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

void createResultsDirectory() {
  if (mkdir("results_experiment", 0755) != 0 && errno != EEXIST) {
    throw std::runtime_error("Failed to create results_experiment directory");
  }
}

void buildTree(IndexTree &tree, const std::vector<int> &keys) {
  for (int rid = 0; rid < static_cast<int>(keys.size()); ++rid) {
    tree.insert(keys[rid], rid);
  }
}

std::vector<int> makeQueries(const std::vector<int> &keys, int seed) {
  std::vector<int> indexes(keys.size());
  std::iota(indexes.begin(), indexes.end(), 0);
  std::mt19937 generator(seed);
  std::shuffle(indexes.begin(), indexes.end(), generator);

  std::vector<int> queries;
  queries.reserve(queryCount);
  for (int i = 0; i < queryCount; ++i) {
    queries.push_back(keys[indexes[i]]);
  }
  return queries;
}

long long searchTree(IndexTree &tree, const std::vector<int> &queries,
                     long long &checksum) {
  auto start = std::chrono::steady_clock::now(); // start timer
  for (int repeat = 0; repeat < searchRepeats; repeat++) {
    for (int key : queries) {
      checksum += tree.search(key);
    }
  }
  auto end = std::chrono::steady_clock::now(); // end timer
  long long batchNs =
      std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
          .count();
  return batchNs / searchRepeats;
}
} // namespace

int runExperiment2() {
  const std::vector<TreeSpec> trees = {
      {"btree", 1}, {"bstar", 2}, {"bplus", 3}};
  const std::vector<int> orders = {3, 5, 10, 16, 32, 64, 128, 256, 512, 1024};

  Dataset dataset = loadDataset("data/student.csv");
  std::vector<int> keys;
  keys.reserve(dataset.size());
  for (int rid = 0; rid < dataset.size(); ++rid) {
    keys.push_back(dataset.getKey(rid));
  }

  std::vector<std::vector<int>> querySets;
  for (int seed = 1; seed <= warmupRuns + maxRuns; ++seed) {
    querySets.push_back(makeQueries(keys, seed));
  }

  createResultsDirectory();
  std::ofstream runFile("results_experiment/experiment2_runs.csv");
  std::ofstream summaryFile("results_experiment/experiment2_summary.csv");
  runFile << std::setprecision(12);
  summaryFile << std::setprecision(12);

  runFile << "tree,order,seed,queries,execution_time_ns,checksum\n";
  summaryFile << "tree,order,records,queries,warmup_runs,measured_runs,"
              << "mean_execution_time_ns,median_execution_time_ns,"
              << "stddev_execution_time_ns,rsd,mean_execution_time_ms,"
              << "median_execution_time_ms,split_count,tree_height,"
              << "num_nodes,num_entries,node_utilization\n";

  std::cout << "Experiment 2: Point Search Performance\n";
  std::cout << "Records: " << keys.size() << ", queries per run: " << queryCount
            << "\n\n";

  for (int order : orders) {
    std::vector<RunState> states;
    for (const TreeSpec &spec : trees) {
      states.emplace_back(&spec);
      states.back().tree = createTree(spec.type, order);
      buildTree(*states.back().tree, keys);
    }

    for (int seed = 1; seed <= warmupRuns; ++seed) {
      for (RunState &state : states) {
        long long checksum = 0;
        searchTree(*state.tree, querySets[seed - 1], checksum);
      }
    }

    for (int seed = warmupRuns + 1; seed <= warmupRuns + maxRuns; ++seed) {
      bool allDone = true;

      for (RunState &state : states) {
        if (state.done) {
          continue;
        }

        long long checksum = 0;
        long long ns = searchTree(*state.tree, querySets[seed - 1], checksum);
        state.executionTimes.push_back(static_cast<double>(ns));
        state.avg = mean(state.executionTimes);
        state.sd = stddev(state.executionTimes, state.avg);
        state.rsd = state.sd / state.avg;

        runFile << state.spec->name << ',' << order << ',' << seed << ','
                << queryCount << ',' << ns << ',' << checksum << '\n';

        if (static_cast<int>(state.executionTimes.size()) >= minMeasuredRuns &&
            state.rsd < targetRsd) {
          state.done = true;
        }
      }

      for (const RunState &state : states) {
        allDone = allDone && state.done;
      }
      if (allDone) {
        break;
      }
    }

    for (const RunState &state : states) {
      double med = median(state.executionTimes);
      double meanMs = state.avg / 1000000.0;
      double medianMs = med / 1000000.0;

      summaryFile << state.spec->name << ',' << order << ',' << keys.size()
                  << ',' << queryCount << ',' << warmupRuns << ','
                  << state.executionTimes.size() << ',' << state.avg << ','
                  << med << ',' << state.sd << ',' << state.rsd << ',' << meanMs
                  << ',' << medianMs << ',' << state.tree->getSplitCount()
                  << ',' << state.tree->getHeight() << ','
                  << state.tree->getNumNode() << ','
                  << state.tree->getNumEntry() << ','
                  << state.tree->getNodeUtilization() << '\n';

      std::cout << state.spec->name << " order=" << order
                << " runs=" << state.executionTimes.size()
                << " median_ms=" << medianMs << " mean_ms=" << meanMs
                << " rsd=" << state.rsd << " height=" << state.tree->getHeight()
                << " utilization=" << state.tree->getNodeUtilization() << "%\n";
    }
  }

  std::cout << "\nSaved results_experiment/experiment2_runs.csv\n";
  std::cout << "Saved results_experiment/experiment2_summary.csv\n";
  return 0;
}
