#include <iostream>
#include <numeric>
#include <gflags/gflags.h>
#include <random>

#include "rocksdb/db.h"
#include "rocksdb/filter_policy.h"
#include "rocksdb/options.h"
#include "rocksdb/monkey_filter.h"
#include "util/string_util.h"
#include "rocksdb/monkey_filter.h"
#include "rocksdb/statistics.h"

DEFINE_string(level_capacities, "", "Comma-separated list of level capacities");
DEFINE_string(run_numbers, "", "Comma-separated list of run numbers");
DEFINE_int32(bpk, 5, "Bits per key for filter");
DEFINE_int32(kvsize, 1024, "Size of key-value pair");
DEFINE_string(compaction_style, "default", "Compaction style");
DEFINE_int32(prepare_entries, 10000000, "Number of entries to prepare");
DEFINE_string(workload, "prepare", "prepare or test");
DEFINE_string(path, "/tmp/db", "dbpath");

inline std::string ItoaWithPadding(const uint64_t key, uint64_t size) {
  std::string key_str = std::to_string(key);
  std::string padding_str(size - key_str.size(), '0');
  key_str = padding_str + key_str;
  return key_str;
}

class KeyGenerator {
 public:
  KeyGenerator(uint64_t start, uint64_t end, uint64_t key_size,
               uint64_t value_size, bool shuffle = true) {
    start_ = start;
    end_ = end;
    idx_ = 0;
    key_size_ = key_size;
    value_size_ = value_size;
    shuffle_ = shuffle;
  }
  std::string Key() const { return ItoaWithPadding(keys_[idx_], key_size_); }
  std::string Value() const {
    return ItoaWithPadding(keys_[idx_], value_size_);
  }
  bool Next() {
    idx_++;
    return idx_ < keys_.size();
  }
  void SeekToFirst() {
    for (uint64_t i = start_; i < end_; i++) {
      keys_.push_back(i);
    }
    if (shuffle_) {
      auto rng = std::default_random_engine{};
      std::shuffle(std::begin(keys_), std::end(keys_), rng);
    }
    idx_ = 0;
  }

 private:
  uint64_t idx_;
  std::vector<uint64_t> keys_;
  uint64_t start_;
  uint64_t end_;
  uint64_t key_size_;
  uint64_t value_size_;
  bool shuffle_;
};

void PrepareDB(rocksdb::DB* db) {
  rocksdb::WriteOptions write_options;
  rocksdb::ReadOptions read_options;
  KeyGenerator key_gen(0, FLAGS_prepare_entries, 24, FLAGS_kvsize - 24);
  key_gen.SeekToFirst();
  int idx = 0;
  while (key_gen.Next()) {
    auto status = db->Put(write_options, key_gen.Key(), key_gen.Value());
    if (!status.ok()) {
      std::cerr << "Failed to put key " << key_gen.Key() << " value "
                << key_gen.Value() << ", because: " << status.ToString() << std::endl;
      exit(1);
    }
    idx ++;
    if (idx % 100000 == 0) {
      std::cout << "prepared: " << idx << " entries" << std::endl;
    }
  }
}

void BalancedWorkload(rocksdb::DB* db) {
  rocksdb::WriteOptions write_options;
  rocksdb::ReadOptions read_options;
  KeyGenerator key_gen(0, FLAGS_prepare_entries, 24, FLAGS_kvsize - 24);
  key_gen.SeekToFirst();
  int idx = 0;
  while (key_gen.Next()) {
    int mo = idx % 4;
    switch (mo) {
      case 0: {
        // get result
        std::string value;
        auto status = db->Get(read_options, key_gen.Key(), &value);
        if (!status.ok()) {
          std::cerr << "Failed to get key " << key_gen.Key() << std::endl;
          exit(1);
        }
        break;
      }
      case 1: {
        // empty result
        auto key = key_gen.Key();
        std::string value;
        key[key.size() - 2] = '_';
        auto status = db->Get(read_options, key, &value);
        if (!status.IsNotFound()) {
          std::cerr << "Found key " << key << " value " << value << std::endl;
          exit(1);
        }
        break;
      }
      case 2: {
        // put
        auto status = db->Put(write_options, key_gen.Key(), key_gen.Value());
        if (!status.ok()) {
          std::cerr << "Failed to put key " << key_gen.Key()
                    << " value " << key_gen.Value() << std::endl;
          exit(1);
        }
        break;
      }
      case 3: {
        // range read
        rocksdb::Iterator* it = db->NewIterator(read_options);
        auto key = key_gen.Key();
        it->Seek(key);
        if (!it->Valid()) {
          std::cerr << "Failed to seek to key " << key << std::endl;
          exit(1);
        }
        for (int i = 0; i < 16 && it->Valid(); i++) {
          it->Next();
        }
        delete it;
        break;
      }
    }
    idx ++;
    if (idx % 100000 == 0) {
      std::cout << "conducted " << idx << " operations" << std::endl;
    }
  }
}

rocksdb::Options get_default_options() {
  rocksdb::Options options;
  options.create_if_missing = true;
  options.write_buffer_size = 2 << 20;
  options.level_compaction_dynamic_level_bytes = false;
  options.max_bytes_for_level_base = options.max_bytes_for_level_base * options.max_bytes_for_level_multiplier;
  auto table_options = options.table_factory->GetOptions<rocksdb::BlockBasedTableOptions>();
  table_options->filter_policy.reset(rocksdb::NewBloomFilterPolicy(FLAGS_bpk));
  return options;
}

rocksdb::Options get_moose_options() {
  rocksdb::Options options;
  options.compaction_style = rocksdb::kCompactionStyleMoose;
  options.create_if_missing = true;
  options.write_buffer_size = 2 << 20;
  options.level_compaction_dynamic_level_bytes = false;
  
  std::vector<std::string> split_st =
      rocksdb::StringSplit(FLAGS_level_capacities, ',');
  std::vector<uint64_t> level_capacities;
  for (auto& s : split_st) {
    level_capacities.push_back(std::stoull(s));
  }
  split_st = rocksdb::StringSplit(FLAGS_run_numbers, ',');
  std::vector<int> run_numbers;
  for (auto& s : split_st) {
    run_numbers.push_back(std::stoi(s));
  }
  std::vector<uint64_t> physical_level_capacities;
  for (int i = 0; i < (int)run_numbers.size(); i++) {
    uint64_t run_size = level_capacities[i] / run_numbers[i];
    for (int j = 0; j < (int)run_numbers[i]; j++) {
      physical_level_capacities.push_back(run_size);
    }
  }
  options.level_capacities = physical_level_capacities;
  options.run_numbers = run_numbers;
  options.num_levels = std::accumulate(run_numbers.begin(), run_numbers.end(), 0);

  uint64_t entry_num = std::accumulate(options.level_capacities.begin() + 1, options.level_capacities.end(), 0UL) / FLAGS_kvsize;
  uint64_t filter_memory = entry_num * FLAGS_bpk / 8;

  auto tmp = rocksdb::MonkeyBpks(entry_num, filter_memory, options.level_capacities, FLAGS_kvsize);
  std::vector<double> bpks = {(double)FLAGS_bpk};
  std::copy(tmp.begin(), tmp.end(), std::back_inserter(bpks));
  // display options
  std::cout << "level capacities: " << std::endl;
  for (auto lvl_cap : options.level_capacities) {
    std::cout << "  " << lvl_cap << std::endl;
  }
  std::cout << "run numbers: " << std::endl;
  for (auto rn : options.run_numbers) {
    std::cout << "  " << rn << std::endl;
  }
  std::cout << "bpks: " << std::endl;
  for (auto bpk : bpks) {
    std::cout << "  " << bpk << std::endl;
  }
  
  auto table_options = options.table_factory->GetOptions<rocksdb::BlockBasedTableOptions>();
  table_options->filter_policy.reset(rocksdb::NewMonkeyFilterPolicy(bpks));

  return options;
}

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  rocksdb::DB* db;

  rocksdb::Options options;
  if (FLAGS_compaction_style == "default") {
    options = get_default_options();
  } else if (FLAGS_compaction_style == "moose") {
    options = get_moose_options();
  } else {
    std::cerr << "Unknown compaction style: " << FLAGS_compaction_style << std::endl;
    return 1;
  }
  if (FLAGS_workload == "test") {
    options.use_direct_io_for_flush_and_compaction = true;
    options.use_direct_reads = true;
  }
  options.level0_slowdown_writes_trigger = 4;
  options.level0_stop_writes_trigger = 8;
  options.statistics = rocksdb::CreateDBStatistics();
  auto status = rocksdb::DB::Open(options, FLAGS_path, &db);
  if (!status.ok()) {
    std::cerr << "Failed to open db: " << status.ToString() << std::endl;
    return 1;
  }
  if (FLAGS_workload == "prepare") {
    PrepareDB(db);
  } else if (FLAGS_workload == "test") {
    BalancedWorkload(db);
  }
  std::string stat;
  db->GetProperty("rocksdb.stats", &stat);
  std::cout << stat << std::endl;
  std::cout << "statistics: " << options.statistics->ToString() << std::endl;

  db->Close();
  delete db;
  return 0;
}