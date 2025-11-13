#include "gflags/gflags.h"

#include "dynamic_test_util.h"
#include "rocksdb/statistics.h"
#include "rocksdb/table.h"
#include "rocksdb/filter_policy.h"

#include <iostream>
#include <string>
#include <sstream>

DEFINE_uint64(buffer_size, 2 * (1<<20), "buffer size");
DEFINE_string(compaction_style, "dynamic", "compaction style");
DEFINE_uint64(window_num, 100, "number of windows");
DEFINE_string(size_ratios, "", "size ratios for Moose");
DEFINE_string(run_numbers, "", "run numbers for Moose");
DEFINE_int32(search_depth, 100, "search depth for DynamicCompaction");
DEFINE_int32(key_size, 24, "key size");
DEFINE_int32(value_size, 1000, "value size");
DEFINE_int32(range_lookup_len, 16, "range lookup length");
DEFINE_string(workload_file, "", "workload file");
DEFINE_int32(sleep_time, 0, "sleep time");
DEFINE_int32(mc_search_len, 400, "mc search length");
DEFINE_int32(parallel, 1, "parallel factor");
DEFINE_int32(cache_size, 32, "cache size in MB");
DEFINE_double(change_threshold, 0.1, "changing threshold of dynamic");
DEFINE_bool(use_continuous, false, "use continuous workload");
DEFINE_bool(use_rangefilter, false, "use range filter");
DEFINE_int32(T, 10, "Leveling size ratio");

template <typename T>
std::vector<T> ParseStringToNumbers(const std::string& src) {
  std::vector<T> res;
  T val;
  std::string tmp = src;
  size_t pos = 0;
  while ((pos = tmp.find(",")) != std::string::npos) {
    std::stringstream ss(tmp.substr(0, pos));
    ss >> val;
    res.push_back(val);
    tmp.erase(0, pos + 1);
  }
  std::stringstream ss(tmp);
  ss >> val;
  res.push_back(val);
  return res;
}

rocksdb::Options GetBasedOptions() {
  rocksdb::Options opt;
  opt.statistics = rocksdb::CreateDBStatistics();
  opt.write_buffer_size = FLAGS_buffer_size;
  opt.create_if_missing = true;
  opt.num_levels = 4;
  opt.force_consistency_checks = false;
  opt.compression = rocksdb::kNoCompression;
  // opt.use_direct_io_for_flush_and_compaction = true;
  opt.max_write_buffer_number = 10;
  // opt.use_direct_reads = true;
  opt.comp_controller = new rocksdb::AtomicCompactionController();
  // get block based table options
  auto table_options = opt.table_factory->GetOptions<rocksdb::BlockBasedTableOptions>();
  table_options->filter_policy.reset(rocksdb::NewBloomFilterPolicy(10, false));
  if (FLAGS_use_rangefilter) {
    table_options->filter_policy.reset(rocksdb::NewDynamicRangeFilter(14, 10));
  }
  auto cache = rocksdb::NewLRUCache(FLAGS_cache_size * (1UL<<20));
  table_options->block_cache = cache;

  return opt;
}

rocksdb::Options GetMooseOptions() {
  rocksdb::Options opt = GetBasedOptions();
  std::vector<double> size_ratios = ParseStringToNumbers<double>(FLAGS_size_ratios);
  std::vector<uint64_t> run_numbers = ParseStringToNumbers<uint64_t>(FLAGS_run_numbers);
  uint64_t prev_capacity = FLAGS_buffer_size * size_ratios[0];
  std::vector<uint64_t> run_sizes{prev_capacity};
  for (size_t i = 1; i < size_ratios.size(); i++) {
    uint64_t cur_capacity = prev_capacity * size_ratios[i];
    uint64_t cur_run_size = cur_capacity / run_numbers[i];
    run_sizes.push_back(cur_run_size);
    prev_capacity = cur_capacity;
  }
  opt.num_levels = 64;
  opt.compaction_style = rocksdb::kCompactionStyleMoose;
  opt.level0_slowdown_writes_trigger = size_ratios[0] + 2;
  opt.level0_stop_writes_trigger = size_ratios[0] + 4;
  opt.comp_controller->InitForMoose(size_ratios, run_numbers, run_sizes);
  return opt;
}

rocksdb::Options GetDynamicOptions() {
  rocksdb::Options opt = GetBasedOptions();
  opt.compaction_style = rocksdb::kCompactionStyleDynamic;
  opt.write_buffer_size = FLAGS_buffer_size;
  opt.delayed_write_rate = 0;

  opt.level0_stop_writes_trigger = 0x7fffffff;
  opt.level0_slowdown_writes_trigger = 0x7fffffff;
  // opt.level0_slowdown_writes_trigger = 30;
  opt.comp_controller->compactioner = new DynCompactionV4::DynamicCompactionerV4(FLAGS_buffer_size);
  opt.comp_controller->compactioner->state_change_threshold = FLAGS_change_threshold;

  return opt;
}

rocksdb::Options GetLevelingOptions() {
  rocksdb::Options opt = GetBasedOptions();
  opt.num_levels = 10;
  opt.target_file_size_base = 64L * (1<<20);
  opt.target_file_size_multiplier = FLAGS_T;
  opt.max_bytes_for_level_multiplier = FLAGS_T;
  opt.max_bytes_for_level_base = FLAGS_buffer_size * opt.max_bytes_for_level_multiplier; // T * F
  opt.level_compaction_dynamic_level_bytes = false;
  std::cout << "Using leveling with T=" << opt.max_bytes_for_level_multiplier << std::endl;
  return opt;
}

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  rocksdb::DB* db;
  rocksdb::Options opt;

  if (FLAGS_compaction_style == "dynamic") {
    opt = GetDynamicOptions();
  } else if (FLAGS_compaction_style == "leveling") {
    opt = GetLevelingOptions();
  } else if (FLAGS_compaction_style == "moose") {
    opt = GetMooseOptions();
  } else {
    std::cout << "unknown compaction style: " << FLAGS_compaction_style << std::endl;
    return 0;
  }
  auto* listener = new DynamicTestListener();
  opt.listeners.emplace_back(listener);
  // opt.listeners.emplace_back(new DynamicTestListener(logger.get()));
  WorkloadManager mng(opt.comp_controller, listener, FLAGS_key_size, 
      FLAGS_value_size, FLAGS_range_lookup_len, FLAGS_buffer_size,
      FLAGS_sleep_time, FLAGS_mc_search_len, FLAGS_parallel, FLAGS_use_continuous);
  
  mng.InitWorkloadFromFile(FLAGS_workload_file, false);
  std::cout << "Finish init workload" << std::endl;

  auto s = rocksdb::DB::Open(opt, "/tmp/db", &db);
  if (!s.ok()) {
    std::cout << "fail to open db: " << s.ToString() << std::endl;
    return 0;
  }
  std::this_thread::sleep_for(std::chrono::seconds(10));
  mng.StartProcessing(db);
  std::cout << "Total finished time: " << mng.total_time << " us" << std::endl;

  for (int i = 0; i < (int)mng.record_times_.size(); i++) {
    if (mng.record_ops_[i] == WorkloadManager::OpType::UPDATE) {
      std::cout << "update time: " << mng.record_times_[i] << std::endl;
    } else if (mng.record_ops_[i] == WorkloadManager::OpType::RANGE_LOOKUP) {
      std::cout << "range lookup time: " << mng.record_times_[i] << std::endl;
    } else {
      std::cout << "point lookup time: " << mng.record_times_[i] << std::endl;
    }
  }
  for (int i = 0; i < (int)mng.window_end_ts_.size(); i++) {
    std::cout << "Window End Ts: # " << i << " " << mng.window_end_ts_[i] << std::endl;
  }
  listener->DisplayCompactionDetails();
  listener->DisplayCompactionIdx();
  std::this_thread::sleep_for(std::chrono::seconds(60));
  std::cout << "Total entry: " << mng.get_db_size(db) << std::endl;
  std::cout << "stats: " << opt.statistics->ToString() << std::endl;
  db->Close();
  return 0;
}