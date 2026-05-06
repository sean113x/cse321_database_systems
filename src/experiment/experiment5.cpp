#include "experiment5.h"

#include "../dataset_handler/dataset.h"
#include "../index_tree/additional/opt_bstar.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {
constexpr int warmupRuns = 10;
constexpr int minMeasuredRuns = 30;
constexpr int maxRuns = 200;
constexpr double targetRsd = 0.02;
constexpr int queryCount = 100000;
constexpr double hotspotProbability = 0.80;
constexpr double hotspotFraction = 0.01;
constexpr double zipfianTheta = 0.99;

enum class WorkloadType {
  Uniform,
  Hotspot,
  Zipfian,
};

struct SearchVariant {
  const char *name;
  OptBStarTree::Options options;
};

struct WorkloadSpec {
  const char *name;
  WorkloadType type;
};

struct SearchResult {
  double executionTimeMs = 0.0;
  long long nodeReadCount = 0;
  long long intraNodeSearchCount = 0;
  long long intraNodeKeyComparisons = 0;
  long long hotKeyCacheHits = 0;
  long long hotKeyCacheMisses = 0;
  int foundCount = 0;
  int height = 0;
  int numNodes = 0;
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

void buildTree(OptBStarTree &tree, const std::vector<int> &keys) {
  for (int rid = 0; rid < static_cast<int>(keys.size()); ++rid) {
    tree.insert(keys[rid], rid);
  }
}

std::vector<int> makeUniformQueries(const std::vector<int> &keys, int seed) {
  std::mt19937 generator(seed);
  std::uniform_int_distribution<int> keyIndex(0, static_cast<int>(keys.size()) -
                                                     1);

  std::vector<int> queries;
  queries.reserve(queryCount);
  for (int i = 0; i < queryCount; ++i) {
    queries.push_back(keys[keyIndex(generator)]);
  }
  return queries;
}

std::vector<int> makeHotspotQueries(const std::vector<int> &keys, int seed) {
  std::vector<int> indexes(keys.size());
  std::iota(indexes.begin(), indexes.end(), 0);

  std::mt19937 hotSetGenerator(2025);
  std::shuffle(indexes.begin(), indexes.end(), hotSetGenerator);

  int hotCount = std::max(1, static_cast<int>(keys.size() * hotspotFraction));
  std::vector<int> hotKeys;
  hotKeys.reserve(hotCount);
  for (int i = 0; i < hotCount; ++i) {
    hotKeys.push_back(keys[indexes[i]]);
  }

  std::mt19937 generator(seed);
  std::bernoulli_distribution chooseHot(hotspotProbability);
  std::uniform_int_distribution<int> hotIndex(
      0, static_cast<int>(hotKeys.size()) - 1);
  std::uniform_int_distribution<int> allIndex(0, static_cast<int>(keys.size()) -
                                                     1);

  std::vector<int> queries;
  queries.reserve(queryCount);
  for (int i = 0; i < queryCount; ++i) {
    if (chooseHot(generator)) {
      queries.push_back(hotKeys[hotIndex(generator)]);
    } else {
      queries.push_back(keys[allIndex(generator)]);
    }
  }
  return queries;
}

std::vector<int> makeZipfianQueries(const std::vector<int> &keys, int seed) {
  std::vector<int> rankedKeys = keys;
  std::mt19937 rankGenerator(2026);
  std::shuffle(rankedKeys.begin(), rankedKeys.end(), rankGenerator);

  std::vector<double> cumulativeProbabilities;
  cumulativeProbabilities.reserve(rankedKeys.size());

  double totalWeight = 0.0;
  for (int rank = 1; rank <= static_cast<int>(rankedKeys.size()); ++rank) {
    totalWeight += 1.0 / std::pow(static_cast<double>(rank), zipfianTheta);
    cumulativeProbabilities.push_back(totalWeight);
  }

  for (double &probability : cumulativeProbabilities) {
    probability /= totalWeight;
  }

  std::mt19937 generator(seed);
  std::uniform_real_distribution<double> probability(0.0, 1.0);

  std::vector<int> queries;
  queries.reserve(queryCount);
  for (int i = 0; i < queryCount; ++i) {
    double sample = probability(generator);
    auto iter = std::lower_bound(cumulativeProbabilities.begin(),
                                 cumulativeProbabilities.end(), sample);
    int index = static_cast<int>(iter - cumulativeProbabilities.begin());
    if (index >= static_cast<int>(rankedKeys.size())) {
      index = static_cast<int>(rankedKeys.size()) - 1;
    }
    queries.push_back(rankedKeys[index]);
  }

  return queries;
}

std::vector<int> makeQueries(const std::vector<int> &keys,
                             const WorkloadSpec &workload, int seed) {
  if (workload.type == WorkloadType::Hotspot) {
    return makeHotspotQueries(keys, seed);
  }
  if (workload.type == WorkloadType::Zipfian) {
    return makeZipfianQueries(keys, seed);
  }
  return makeUniformQueries(keys, seed);
}

SearchResult runSearchWorkload(const SearchVariant &variant, int order,
                               const std::vector<int> &keys,
                               const std::vector<int> &queries) {
  OptBStarTree tree(order, variant.options);
  buildTree(tree, keys);
  tree.resetNodeReadCount();
  tree.resetMetrics(true);

  int foundCount = 0;
  auto start = std::chrono::steady_clock::now();
  for (int key : queries) {
    if (tree.search(key) != -1) {
      foundCount++;
    }
  }
  auto end = std::chrono::steady_clock::now();

  OptBStarTree::Metrics metrics = tree.getMetrics();
  SearchResult result;
  result.executionTimeMs =
      std::chrono::duration<double, std::milli>(end - start).count();
  result.nodeReadCount = tree.getNodeReadCount();
  result.intraNodeSearchCount = metrics.intraNodeSearchCount;
  result.intraNodeKeyComparisons = metrics.intraNodeKeyComparisons;
  result.hotKeyCacheHits = metrics.hotKeyCacheHits;
  result.hotKeyCacheMisses = metrics.hotKeyCacheMisses;
  result.foundCount = foundCount;
  result.height = tree.getHeight();
  result.numNodes = tree.getNumNode();
  result.nodeUtilization = tree.getNodeUtilization();
  return result;
}

void writeRun(std::ofstream &file, const SearchVariant &variant, int order,
              const WorkloadSpec &workload, int seed,
              const SearchResult &result) {
  file << variant.name << ',' << order << ',' << workload.name << ','
       << queryCount << ',' << seed << ',' << result.executionTimeMs << ','
       << result.nodeReadCount << ',' << result.intraNodeSearchCount << ','
       << result.intraNodeKeyComparisons << ',' << result.hotKeyCacheHits << ','
       << result.hotKeyCacheMisses << ',' << result.foundCount << ','
       << result.height << ',' << result.numNodes << ','
       << result.nodeUtilization << '\n';
}

void writeSummary(std::ofstream &file, const SearchVariant &variant, int order,
                  const WorkloadSpec &workload,
                  const std::vector<SearchResult> &results, double avg,
                  double sd, double rsd) {
  std::vector<double> executionTimes;
  std::vector<long long> nodeReads;
  std::vector<long long> intraNodeSearches;
  std::vector<long long> comparisons;
  std::vector<long long> hotHits;
  std::vector<long long> hotMisses;

  executionTimes.reserve(results.size());
  nodeReads.reserve(results.size());
  intraNodeSearches.reserve(results.size());
  comparisons.reserve(results.size());
  hotHits.reserve(results.size());
  hotMisses.reserve(results.size());

  for (const SearchResult &result : results) {
    executionTimes.push_back(result.executionTimeMs);
    nodeReads.push_back(result.nodeReadCount);
    intraNodeSearches.push_back(result.intraNodeSearchCount);
    comparisons.push_back(result.intraNodeKeyComparisons);
    hotHits.push_back(result.hotKeyCacheHits);
    hotMisses.push_back(result.hotKeyCacheMisses);
  }

  const SearchResult &last = results.back();
  file << variant.name << ',' << order << ',' << workload.name << ','
       << queryCount << ',' << warmupRuns << ',' << results.size() << ',' << avg
       << ',' << median(executionTimes) << ',' << sd << ',' << rsd << ','
       << meanLong(nodeReads) << ',' << meanLong(intraNodeSearches) << ','
       << meanLong(comparisons) << ',' << meanLong(hotHits) << ','
       << meanLong(hotMisses) << ',' << last.foundCount << ',' << last.height
       << ',' << last.numNodes << ',' << last.nodeUtilization << '\n';
}
} // namespace

int runExperiment5() {
  OptBStarTree::Options linearOptions;
  linearOptions.nodeSearchPolicy = OptBStarTree::NodeSearchPolicy::Linear;
  linearOptions.enableHotKeyCache = false;
  linearOptions.overflowPolicy =
      OptBStarTree::OverflowPolicy::EagerRedistribution;

  OptBStarTree::Options binaryOptions = linearOptions;
  binaryOptions.nodeSearchPolicy = OptBStarTree::NodeSearchPolicy::Binary;

  OptBStarTree::Options hotCacheOptions = binaryOptions;
  hotCacheOptions.enableHotKeyCache = true;
  hotCacheOptions.hotKeyCacheCapacity = 64;

  const std::vector<SearchVariant> variants = {
      {"linear", linearOptions},
      {"binary", binaryOptions},
      {"binary_hot_cache", hotCacheOptions},
  };
  const std::vector<WorkloadSpec> workloads = {
      {"uniform", WorkloadType::Uniform},
      {"hotspot_80_20", WorkloadType::Hotspot},
      {"zipfian_theta_0_99", WorkloadType::Zipfian},
  };
  const std::vector<int> orders = {16, 32, 64, 128, 256, 512, 1024};

  Dataset dataset = loadDataset("data/student.csv");
  std::vector<int> keys;
  keys.reserve(dataset.size());
  for (int rid = 0; rid < dataset.size(); ++rid) {
    keys.push_back(dataset.getKey(rid));
  }

  createResultsDirectory();
  std::ofstream runFile("results_experiment/experiment5_runs.csv");
  std::ofstream summaryFile("results_experiment/experiment5_summary.csv");
  runFile << std::setprecision(12);
  summaryFile << std::setprecision(12);

  runFile << "variant,order,workload,query_count,seed,execution_time_ms,"
          << "node_read_count,intra_node_search_count,"
          << "intra_node_key_comparisons,hot_key_cache_hits,"
          << "hot_key_cache_misses,found_count,height,num_nodes,"
          << "node_utilization\n";
  summaryFile << "variant,order,workload,query_count,warmup_runs,"
              << "measured_runs,"
              << "mean_execution_time_ms,median_execution_time_ms,"
              << "stddev_execution_time_ms,rsd,"
              << "mean_node_read_count,mean_intra_node_search_count,"
              << "mean_intra_node_key_comparisons,mean_hot_key_cache_hits,"
              << "mean_hot_key_cache_misses,found_count,height,num_nodes,"
              << "node_utilization\n";

  std::cout << "Experiment 5: Intra-node Search Optimization\n";
  std::cout << "Records: " << keys.size() << ", queries/run=" << queryCount
            << ", warmup_runs=" << warmupRuns
            << ", min_measured_runs=" << minMeasuredRuns
            << ", max_runs=" << maxRuns << ", target_rsd=" << targetRsd
            << "\n\n";

  for (const WorkloadSpec &workload : workloads) {
    std::vector<std::vector<int>> queryBatches;
    queryBatches.reserve(warmupRuns + maxRuns);
    for (int seed = 1; seed <= warmupRuns + maxRuns; ++seed) {
      queryBatches.push_back(makeQueries(keys, workload, seed));
    }

    for (int order : orders) {
      for (const SearchVariant &variant : variants) {
        for (int seed = 1; seed <= warmupRuns; ++seed) {
          runSearchWorkload(variant, order, keys, queryBatches[seed - 1]);
        }

        std::vector<SearchResult> results;
        results.reserve(maxRuns);
        double avg = 0.0;
        double sd = 0.0;
        double rsd = 0.0;

        for (int seed = warmupRuns + 1; seed <= warmupRuns + maxRuns; ++seed) {
          SearchResult result =
              runSearchWorkload(variant, order, keys, queryBatches[seed - 1]);
          writeRun(runFile, variant, order, workload, seed, result);
          results.push_back(result);

          std::vector<double> executionTimes;
          executionTimes.reserve(results.size());
          for (const SearchResult &measuredResult : results) {
            executionTimes.push_back(measuredResult.executionTimeMs);
          }
          avg = mean(executionTimes);
          sd = stddev(executionTimes, avg);
          rsd = avg == 0.0 ? 0.0 : sd / avg;

          if (static_cast<int>(results.size()) >= minMeasuredRuns &&
              rsd < targetRsd) {
            break;
          }
        }

        writeSummary(summaryFile, variant, order, workload, results, avg, sd,
                     rsd);

        std::vector<double> executionTimes;
        executionTimes.reserve(results.size());
        for (const SearchResult &result : results) {
          executionTimes.push_back(result.executionTimeMs);
        }
        const SearchResult &last = results.back();
        std::cout << variant.name << " order=" << order
                  << " workload=" << workload.name << " runs=" << results.size()
                  << " mean_ms=" << avg
                  << " median_ms=" << median(executionTimes) << " rsd=" << rsd
                  << " comparisons=" << last.intraNodeKeyComparisons
                  << " hot_hits=" << last.hotKeyCacheHits
                  << " hot_misses=" << last.hotKeyCacheMisses
                  << " height=" << last.height << '\n';
      }
    }
    std::cout << '\n';
  }

  std::cout << "Saved results_experiment/experiment5_runs.csv\n";
  std::cout << "Saved results_experiment/experiment5_summary.csv\n";
  return 0;
}
