#include "experiment3.h"

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
constexpr int maxRuns = 200;
constexpr int queryRepeats = 10;
constexpr double targetRsd = 0.02;

struct TreeSpec {
  const char *name;
  int type;
};

struct QuerySpec {
  const char *name;
  int startKey;
  int endKey;
};

struct QueryResult {
  int rangeCount = 0;
  int maleCount = 0;
  double avgGpa = 0.0;
  double avgHeight = 0.0;
};

struct RunState {
  const TreeSpec *spec;
  std::unique_ptr<IndexTree> tree;
  std::vector<double> executionTimes;
  QueryResult result;
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

void calculateQueryResult(const std::vector<int> &rids, const Dataset &dataset,
                          int genderIndex, int gpaIndex, int heightIndex,
                          QueryResult &result) {
  double gpaSum = 0.0;
  double heightSum = 0.0;
  int maleCount = 0;

  for (int rid : rids) {
    if (dataset.getValue(rid, genderIndex) == "Male") {
      gpaSum += std::stod(dataset.getValue(rid, gpaIndex));
      heightSum += std::stod(dataset.getValue(rid, heightIndex));
      maleCount++;
    }
  }

  result.rangeCount = static_cast<int>(rids.size());
  result.maleCount = maleCount;
  result.avgGpa = maleCount == 0 ? 0.0 : gpaSum / maleCount;
  result.avgHeight = maleCount == 0 ? 0.0 : heightSum / maleCount;
}

long long runQueryBatch(IndexTree &tree, const QuerySpec &query,
                        std::vector<int> &rids) {
  auto start = std::chrono::steady_clock::now();
  for (int repeat = 0; repeat < queryRepeats; repeat++) {
    rids = tree.range_query(query.startKey, query.endKey);
  }

  auto end = std::chrono::steady_clock::now();
  long long batchNs =
      std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
          .count();
  return batchNs / queryRepeats;
}
} // namespace

int runExperiment3() {
  const std::vector<TreeSpec> trees = {
      {"btree", 1}, {"bstar", 2}, {"bplus", 3}};
  const std::vector<int> orders = {3, 5, 10, 16, 32, 64, 128, 256, 512, 1024};
  const std::vector<QuerySpec> queries = {
      {"id_202000000_202100000", 202000000, 202100000},
      {"id_202000000_202600000", 202000000, 202600000}};

  Dataset dataset = loadDataset("data/student.csv");
  int genderIndex = dataset.getColumnIndex("Gender");
  int gpaIndex = dataset.getColumnIndex("GPA");
  int heightIndex = dataset.getColumnIndex("Height");

  std::vector<int> keys;
  keys.reserve(dataset.size());
  for (int rid = 0; rid < dataset.size(); ++rid) {
    keys.push_back(dataset.getKey(rid));
  }

  createResultsDirectory();
  std::ofstream runFile("results_experiment/experiment3_runs.csv");
  std::ofstream summaryFile("results_experiment/experiment3_summary.csv");
  runFile << std::setprecision(12);
  summaryFile << std::setprecision(12);

  runFile << "tree,order,query,start_key,end_key,run,execution_time_ns,"
          << "range_count,male_count,avg_gpa,avg_height\n";
  summaryFile << "tree,order,query,start_key,end_key,records,warmup_runs,"
              << "measured_runs,mean_execution_time_ns,"
              << "median_execution_time_ns,stddev_execution_time_ns,rsd,"
              << "mean_execution_time_ms,median_execution_time_ms,"
              << "range_count,male_count,avg_gpa,avg_height,tree_height,"
              << "node_utilization\n";

  std::cout << "Experiment 3: Range Query Performance\n";
  std::cout << "Records: " << keys.size() << "\n\n";

  for (const QuerySpec &query : queries) {
    std::cout << "Query: " << query.name << '\n';

    for (int order : orders) {
      std::vector<RunState> states;
      for (const TreeSpec &spec : trees) {
        states.emplace_back(&spec);
        states.back().tree = createTree(spec.type, order);
        buildTree(*states.back().tree, keys);
      }

      for (int run = 0; run < warmupRuns; ++run) {
        for (RunState &state : states) {
          std::vector<int> rids;
          runQueryBatch(*state.tree, query, rids);
        }
      }

      for (int run = 1; run <= maxRuns; ++run) {
        bool allDone = true;

        for (RunState &state : states) {
          if (state.done) {
            continue;
          }

          std::vector<int> rids;
          long long ns = runQueryBatch(*state.tree, query, rids);
          calculateQueryResult(rids, dataset, genderIndex, gpaIndex,
                               heightIndex, state.result);
          state.executionTimes.push_back(static_cast<double>(ns));
          state.avg = mean(state.executionTimes);
          state.sd = stddev(state.executionTimes, state.avg);
          state.rsd = state.sd / state.avg;

          runFile << state.spec->name << ',' << order << ',' << query.name
                  << ',' << query.startKey << ',' << query.endKey << ',' << run
                  << ',' << ns << ',' << state.result.rangeCount << ','
                  << state.result.maleCount << ',' << state.result.avgGpa << ','
                  << state.result.avgHeight << '\n';

          if (run >= minMeasuredRuns && state.rsd < targetRsd) {
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

        summaryFile << state.spec->name << ',' << order << ',' << query.name
                    << ',' << query.startKey << ',' << query.endKey << ','
                    << keys.size() << ',' << warmupRuns << ','
                    << state.executionTimes.size() << ',' << state.avg << ','
                    << med << ',' << state.sd << ',' << state.rsd << ','
                    << meanMs << ',' << medianMs << ','
                    << state.result.rangeCount << ',' << state.result.maleCount
                    << ',' << state.result.avgGpa << ','
                    << state.result.avgHeight << ',' << state.tree->getHeight()
                    << ',' << state.tree->getNodeUtilization() << '\n';

        std::cout << state.spec->name << " order=" << order
                  << " query=" << query.name
                  << " runs=" << state.executionTimes.size()
                  << " median_ms=" << medianMs << " mean_ms=" << meanMs
                  << " rsd=" << state.rsd
                  << " range_count=" << state.result.rangeCount
                  << " male_count=" << state.result.maleCount << '\n';
      }
    }

    std::cout << '\n';
  }

  std::cout << "\nSaved results_experiment/experiment3_runs.csv\n";
  std::cout << "Saved results_experiment/experiment3_summary.csv\n";
  return 0;
}
