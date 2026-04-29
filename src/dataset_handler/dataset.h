#pragma once

#include <string>
#include <vector>

struct Record {
  std::vector<std::string> values;
};

class Dataset {
private:
  std::vector<std::string> headers;
  std::vector<Record> records;
  int keyIndex;

  const Record &findRecord(int rid) const;

public:
  explicit Dataset(std::vector<std::string> headers);

  void addRecord(std::vector<std::string> values);
  int size() const;
  const std::string &getKeyHeader() const;
  int getColumnIndex(const std::string &header) const;
  int getKey(int rid) const;
  const std::string &getValue(int rid, int columnIndex) const;
  std::string getRecordString(int rid) const;
};

Dataset loadDataset(const std::string &path);
