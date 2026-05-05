#include "experiment6.h"

#include "../dataset_handler/dataset.h"
#include "../index_tree/additional/opt_bstar.h"
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
constexpr double targetRsd = 0.02;
constexpr double selectiveAlpha = 0.25;

enum class VariantType {
  BTree,
  OptBStar,
};

struct InsertVariant {
  const char *name;
  VariantType type;
  OptBStarTree::Options options;
};

struct InsertResult {
  double insertionTimeMs = 0.0;
  int splitCount = 0;
  long long redistributionCount = 0;
  long long forcedRedistributionCount = 0;
  long long skippedRedistributionCount = 0;
  long long twoToThreeSplitCount = 0;
  long long rootSplitCount = 0;
  long long redistributionMovedEntries = 0;
  int height = 0;
  int numNodes = 0;
  int numEntries = 0;
  double nodeUtilization = 0.0;
};

double mean(const std::vector<double> &values) {
  double sum = 0.0;
  for (double value : values) {
    sum += value;
  }
  return sum / static_cast<double>(values.size());
}

double meanLong(const std::vector<long long> &values) {
  long long sum = 0;
  for (long long value : values) {
    sum += value;
  }
  return static_cast<double>(sum) / static_cast<double>(values.size());
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

void insertAll(IndexTree &tree, const std::vector<int> &keys) {
  for (int rid = 0; rid < static_cast<int>(keys.size()); ++rid) {
    tree.insert(keys[rid], rid);
  }
}

InsertResult runBTreeInsertion(int order, const std::vector<int> &keys) {
  BTree tree(order);

  auto start = std::chrono::steady_clock::now();
  insertAll(tree, keys);
  auto end = std::chrono::steady_clock::now();

  InsertResult result;
  result.insertionTimeMs =
      std::chrono::duration<double, std::milli>(end - start).count();
  result.splitCount = tree.getSplitCount();
  result.height = tree.getHeight();
  result.numNodes = tree.getNumNode();
  result.numEntries = tree.getNumEntry();
  result.nodeUtilization = tree.getNodeUtilization();
  return result;
}

InsertResult runOptBStarInsertion(const InsertVariant &variant, int order,
                                  const std::vector<int> &keys) {
  OptBStarTree tree(order, variant.options);

  auto start = std::chrono::steady_clock::now();
  insertAll(tree, keys);
  auto end = std::chrono::steady_clock::now();

  OptBStarTree::Metrics metrics = tree.getMetrics();
  InsertResult result;
  result.insertionTimeMs =
      std::chrono::duration<double, std::milli>(end - start).count();
  result.splitCount = tree.getSplitCount();
  result.redistributionCount = metrics.redistributionCount;
  result.forcedRedistributionCount = metrics.forcedRedistributionCount;
  result.skippedRedistributionCount = metrics.skippedRedistributionCount;
  result.twoToThreeSplitCount = metrics.twoToThreeSplitCount;
  result.rootSplitCount = metrics.rootSplitCount;
  result.redistributionMovedEntries = metrics.redistributionMovedEntries;
  result.height = tree.getHeight();
  result.numNodes = tree.getNumNode();
  result.numEntries = tree.getNumEntry();
  result.nodeUtilization = tree.getNodeUtilization();
  return result;
}

InsertResult runInsertion(const InsertVariant &variant, int order,
                          const std::vector<int> &keys) {
  if (variant.type == VariantType::BTree) {
    return runBTreeInsertion(order, keys);
  }
  return runOptBStarInsertion(variant, order, keys);
}

void writeRun(std::ofstream &file, const InsertVariant &variant, int order,
              int run, int records, const InsertResult &result) {
  file << variant.name << ',' << order << ',' << run << ',' << records << ','
       << result.insertionTimeMs << ',' << result.splitCount << ','
       << result.redistributionCount << ','
       << result.forcedRedistributionCount << ','
       << result.skippedRedistributionCount << ','
       << result.twoToThreeSplitCount << ',' << result.rootSplitCount << ','
       << result.redistributionMovedEntries << ',' << result.height << ','
       << result.numNodes << ',' << result.numEntries << ','
       << result.nodeUtilization << '\n';
}

void writeSummary(std::ofstream &file, const InsertVariant &variant, int order,
                  int records, const std::vector<InsertResult> &results,
                  double avg, double sd, double rsd) {
  std::vector<double> insertionTimes;
  std::vector<long long> redistributionCounts;
  std::vector<long long> forcedRedistributions;
  std::vector<long long> skippedRedistributions;
  std::vector<long long> twoToThreeSplits;
  std::vector<long long> movedEntries;

  insertionTimes.reserve(results.size());
  redistributionCounts.reserve(results.size());
  forcedRedistributions.reserve(results.size());
  skippedRedistributions.reserve(results.size());
  twoToThreeSplits.reserve(results.size());
  movedEntries.reserve(results.size());

  for (const InsertResult &result : results) {
    insertionTimes.push_back(result.insertionTimeMs);
    redistributionCounts.push_back(result.redistributionCount);
    forcedRedistributions.push_back(result.forcedRedistributionCount);
    skippedRedistributions.push_back(result.skippedRedistributionCount);
    twoToThreeSplits.push_back(result.twoToThreeSplitCount);
    movedEntries.push_back(result.redistributionMovedEntries);
  }

  const InsertResult &last = results.back();
  file << variant.name << ',' << order << ',' << records << ','
       << warmupRuns << ',' << results.size() << ',' << avg << ','
       << median(insertionTimes) << ',' << sd << ',' << rsd << ','
       << last.splitCount << ','
       << meanLong(redistributionCounts) << ','
       << meanLong(forcedRedistributions) << ','
       << meanLong(skippedRedistributions) << ','
       << meanLong(twoToThreeSplits) << ',' << last.rootSplitCount << ','
       << meanLong(movedEntries) << ','
       << last.height << ',' << last.numNodes << ',' << last.numEntries << ','
       << last.nodeUtilization << '\n';
}
} // namespace

int runExperiment6() {
  OptBStarTree::Options eagerOptions;
  eagerOptions.nodeSearchPolicy = OptBStarTree::NodeSearchPolicy::Linear;
  eagerOptions.enableHotKeyCache = false;
  eagerOptions.overflowPolicy =
      OptBStarTree::OverflowPolicy::EagerRedistribution;

  OptBStarTree::Options selectiveOptions = eagerOptions;
  selectiveOptions.overflowPolicy =
      OptBStarTree::OverflowPolicy::SelectiveRedistribution;
  selectiveOptions.selectiveRedistributionAlpha = selectiveAlpha;

  const std::vector<InsertVariant> variants = {
      {"btree", VariantType::BTree, {}},
      {"bstar_eager", VariantType::OptBStar, eagerOptions},
      {"bstar_selective_alpha_0_25", VariantType::OptBStar, selectiveOptions},
  };
  const std::vector<int> orders = {10, 20, 50, 100};

  Dataset dataset = loadDataset("data/student.csv");
  std::vector<int> keys;
  keys.reserve(dataset.size());
  for (int rid = 0; rid < dataset.size(); ++rid) {
    keys.push_back(dataset.getKey(rid));
  }

  createResultsDirectory();
  std::ofstream runFile(
      "results_experiment/experiment6_selective_redistribution_runs.csv");
  std::ofstream summaryFile(
      "results_experiment/experiment6_selective_redistribution_summary.csv");
  runFile << std::setprecision(12);
  summaryFile << std::setprecision(12);

  runFile << "variant,order,run,records,insertion_time_ms,split_count,"
          << "redistribution_count,forced_redistribution_count,"
          << "skipped_redistribution_count,two_to_three_split_count,"
          << "root_split_count,"
          << "redistribution_moved_entries,height,total_node_count,"
          << "num_entries,node_utilization\n";
  summaryFile << "variant,order,records,warmup_runs,measured_runs,"
              << "mean_insertion_time_ms,median_insertion_time_ms,"
              << "stddev_insertion_time_ms,rsd,"
              << "split_count,mean_redistribution_count,"
              << "mean_forced_redistribution_count,"
              << "mean_skipped_redistribution_count,"
              << "mean_two_to_three_split_count,root_split_count,"
              << "mean_redistribution_moved_entries,height,"
              << "total_node_count,num_entries,node_utilization\n";

  std::cout << "Experiment 6: Selective Redistribution in B*-tree\n";
  std::cout << "Records: " << keys.size()
            << ", warmup_runs=" << warmupRuns
            << ", min_measured_runs=" << minMeasuredRuns
            << ", max_runs=" << maxRuns << ", target_rsd=" << targetRsd
            << ", alpha=" << selectiveAlpha << "\n\n";

  for (int order : orders) {
    for (const InsertVariant &variant : variants) {
      for (int run = 0; run < warmupRuns; ++run) {
        runInsertion(variant, order, keys);
      }

      std::vector<InsertResult> results;
      results.reserve(maxRuns);
      double avg = 0.0;
      double sd = 0.0;
      double rsd = 0.0;

      for (int run = 1; run <= maxRuns; ++run) {
        InsertResult result = runInsertion(variant, order, keys);
        writeRun(runFile, variant, order, run, static_cast<int>(keys.size()),
                 result);
        results.push_back(result);

        std::vector<double> insertionTimes;
        insertionTimes.reserve(results.size());
        for (const InsertResult &measuredResult : results) {
          insertionTimes.push_back(measuredResult.insertionTimeMs);
        }
        avg = mean(insertionTimes);
        sd = stddev(insertionTimes, avg);
        rsd = avg == 0.0 ? 0.0 : sd / avg;

        if (static_cast<int>(results.size()) >= minMeasuredRuns &&
            rsd < targetRsd) {
          break;
        }
      }

      writeSummary(summaryFile, variant, order, static_cast<int>(keys.size()),
                   results, avg, sd, rsd);

      std::vector<double> insertionTimes;
      insertionTimes.reserve(results.size());
      for (const InsertResult &result : results) {
        insertionTimes.push_back(result.insertionTimeMs);
      }
      const InsertResult &last = results.back();
      std::cout << variant.name << " order=" << order
                << " runs=" << results.size() << " mean_ms=" << avg
                << " median_ms=" << median(insertionTimes)
                << " rsd=" << rsd
                << " splits=" << last.splitCount
                << " redistributions=" << last.redistributionCount
                << " forced_redistributions="
                << last.forcedRedistributionCount
                << " two_to_three=" << last.twoToThreeSplitCount
                << " moved_entries=" << last.redistributionMovedEntries
                << " nodes=" << last.numNodes << " height=" << last.height
                << " utilization=" << last.nodeUtilization << '\n';
    }
    std::cout << '\n';
  }

  std::cout
      << "Saved results_experiment/experiment6_selective_redistribution_runs.csv\n";
  std::cout << "Saved "
               "results_experiment/experiment6_selective_redistribution_"
               "summary.csv\n";
  return 0;
}
