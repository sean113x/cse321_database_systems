#include "dataset.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {
std::vector<std::string> splitLine(const std::string &line) {
  std::vector<std::string> fields;
  std::stringstream ss(line);
  std::string field;

  while (std::getline(ss, field, ',')) {
    if (!field.empty() && field.back() == '\r') {
      field.pop_back();
    }
    fields.push_back(field);
  }

  return fields;
}

} // namespace

Dataset::Dataset(std::vector<std::string> headers)
    : headers(std::move(headers)), keyIndex(0) {}

void Dataset::addRecord(std::vector<std::string> values) {
  records.push_back({std::move(values)});
}

int Dataset::size() const { return static_cast<int>(records.size()); }

const std::string &Dataset::getKeyHeader() const { return headers[keyIndex]; }

int Dataset::getColumnIndex(const std::string &header) const {
  for (int index = 0; index < static_cast<int>(headers.size()); ++index) {
    if (headers[index] == header) {
      return index;
    }
  }

  throw std::runtime_error("Column does not exist: " + header);
}

int Dataset::getKey(int rid) const {
  const Record &record = findRecord(rid);

  if (keyIndex < 0 || keyIndex >= static_cast<int>(record.values.size())) {
    throw std::runtime_error("Record does not have a key value");
  }

  return std::stoi(record.values[keyIndex]);
}

const std::string &Dataset::getValue(int rid, int columnIndex) const {
  const Record &record = findRecord(rid);

  if (columnIndex < 0 || columnIndex >= static_cast<int>(record.values.size())) {
    throw std::runtime_error("Record does not have the requested value");
  }

  return record.values[columnIndex];
}

std::string Dataset::getRecordString(int rid) const {
  const Record &record = findRecord(rid);
  std::ostringstream out;
  out << "rid=" << rid;

  for (int index = 0; index < static_cast<int>(headers.size()); ++index) {
    out << ", " << headers[index] << "=";
    if (index < static_cast<int>(record.values.size())) {
      out << record.values[index];
    }
  }

  return out.str();
}

const Record &Dataset::findRecord(int rid) const {
  if (rid < 0 || rid >= static_cast<int>(records.size())) {
    throw std::runtime_error("Invalid RID");
  }

  return records[rid];
}

Dataset loadDataset(const std::string &path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open dataset: " + path);
  }

  std::string line;

  if (!std::getline(file, line)) {
    throw std::runtime_error("Dataset is empty: " + path);
  }

  std::vector<std::string> headers = splitLine(line);
  if (headers.empty()) {
    throw std::runtime_error("Dataset header is empty: " + path);
  }

  Dataset dataset(headers);

  while (std::getline(file, line)) {
    dataset.addRecord(splitLine(line));
  }

  return dataset;
}
