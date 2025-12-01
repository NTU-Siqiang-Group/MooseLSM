#include "gflags/gflags.h"
#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/statistics.h"
#include "rocksdb/filter_policy.h"
#include "rocksdb/table.h"
#include "rocksdb/listener.h"
#include "rocksdb/dynamic_thread_pool.h"

#include <iostream>
#include <string>
#include <sstream>
#include <tbb/concurrent_queue.h>
#include <fstream>
#include <random>

DEFINE_string(db_path, "/tmp/db", "db path");
DEFINE_uint64(buffer_size, 64 * (1<<20), "write buffer size");
DEFINE_string(compaction_style, "dynamic", "compaction style");
DEFINE_uint64(simulation_iter, 400, "simulation iterations");
DEFINE_int32(parallel, 1, "parallel factor");
DEFINE_string(output_file, "arce.log", "output log stream");
DEFINE_string(benchmark_file, "", "benchmarking workload");
DEFINE_int32(key_size, 24, "key size");
DEFINE_int32(value_size, 1000, "value size");
DEFINE_double(read_ratio, 0.99, "read ratio");
DEFINE_double(write_ratio, 0.01, "write ratio");
DEFINE_int64(test_size, 1e8, "total executed operations");

#define MAX_KEY_VAL 999999999

struct WorkloadGenerator {
  std::string workload_file;
  std::ifstream ifs;
  bool finished = false;
  std::vector<uint64_t> existing_keys;
  double rratio, wratio;
  int64_t workload_size;
  int64_t gen_op_cnt = 0;
  
  int workload_buffer = 1000000;
  std::atomic<bool> producing{true};

  std::uniform_real_distribution<> op_dis;
  std::mt19937 gen;
  std::uniform_int_distribution<uint64_t> key_dis;

  tbb::concurrent_queue<std::pair<std::string, std::string>> workloads;

  WorkloadGenerator(const std::string& input, double read_ratio, double write_ratio, int64_t tsize):
    workload_file(input),
    rratio(read_ratio),
    wratio(write_ratio),
    workload_size(tsize) {
    if (workload_file.length() > 0) {
      ifs = std::ifstream(workload_file);
    } else {
      std::random_device rd;
      gen = std::mt19937(rd());
      op_dis = std::uniform_real_distribution<>(0.0, 1.0);
      key_dis = std::uniform_int_distribution<uint64_t>(0, MAX_KEY_VAL);
    }
  }
  
  void StopProduce() {
    producing.store(false);
  }

  bool Valid() {
    if (workload_file.length() > 0) {
      return !ifs.eof();
    }
    return gen_op_cnt < workload_size;
  }

  void Produce() {
    if (workload_file.length() > 0) return;
    while (producing.load()) {
      if ((int)workloads.unsafe_size() > workload_buffer) {
        std::this_thread::sleep_for(std::chrono::microseconds(10));
        continue;
      }
      std::string op;
      double r = op_dis(gen);
      if (r < rratio) {
        // read
        op = "READ";
        // randomly choose a key in existing keys
        if (existing_keys.empty()) {
          // randomly choose one by key_dis
          uint64_t ikey = key_dis(gen);
          workloads.emplace(std::make_pair(op, std::to_string(ikey)));
        } else {
          std::uniform_int_distribution<> set_dis(0, existing_keys.size() - 1);
          int index = set_dis(gen);
          workloads.emplace(std::make_pair(op, std::to_string(existing_keys[index])));
        }
      } else {
        op = "INSERT";
        uint64_t ikey = key_dis(gen);
        existing_keys.push_back(ikey);
        workloads.emplace(std::make_pair(op, std::to_string(ikey)));
      }
    }
  }
  
  std::pair<std::string, std::string> Next() {
    std::string op, key;
    if (workload_file.length() > 0) {
      ifs >> op >> key;
      return std::make_pair(op, key);
    }
    std::pair<std::string, std::string> ret;
    while (workloads.empty());
    workloads.try_pop(ret);
    gen_op_cnt ++;
    return ret;
  }
};

struct SimpleLog {
  std::string op;
  int32_t operand;
};

std::ostream& operator<<(std::ostream& out, const SimpleLog& l) {
  return out << l.op << ": " << l.operand;
}


template <typename LogType>
struct Logger {
  tbb::concurrent_queue<LogType> receiver;
  std::string outputfile;
  std::ofstream outstream;
  std::atomic<bool> end_logging{false};
  std::atomic<bool> ended{false};

  void Log(const LogType& log) {
    receiver.push(log);
  }

  void EndLogging() {
    end_logging.store(true);
  }

  bool IsEnded() {
    return ended.load();
  }

  void StartLogging() {
    ended.store(false);
    end_logging.store(false);
    while (!end_logging.load()) {
      if (!receiver.empty()) {
        LogType l;
        if (receiver.try_pop(l)) {
          outstream << l << std::endl;
        }
      }
    }
    ended.store(true);
  }

  Logger(const std::string& out): outputfile(out) {
    outstream = std::ofstream(out);
  }

  ~Logger() {
    EndLogging();
    while (!IsEnded());
    outstream.close();
  }
};

void PadStringWithPrefix(int length, std::string& str) {
  if ((int)str.size() < length) {
    str.insert(0, length - str.size(), '0');
  }
}

void RunBenchmark(WorkloadGenerator& gen, rocksdb::DB* db, Logger<SimpleLog>* logger) {
  while (gen.Valid()) {
    auto [op, origin_key] = gen.Next();
    int duration_in_nano = 0;
    if (op == "INSERT") {
      // PUT
      std::string key(origin_key), val(origin_key);
      PadStringWithPrefix(FLAGS_key_size, key);
      PadStringWithPrefix(FLAGS_value_size, val);
      auto start = std::chrono::steady_clock::now();
      auto status = db->Put(rocksdb::WriteOptions(), key, val);
      if (!status.ok()) {
        // FAIL
        std::cout << "Fail to insert key: " << key << ", because: " << status.ToString() << std::endl;
        exit(1);
      }
      auto end = std::chrono::steady_clock::now();
      duration_in_nano = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    } else if (op == "SCAN") {
      // RANGE
    
    } else if (op == "READ") {
      // POINT
      std::string key(origin_key);
      std::string val;
      PadStringWithPrefix(FLAGS_key_size, key);
      auto start = std::chrono::steady_clock::now();
      auto status = db->Get(rocksdb::ReadOptions(), key, &val);
      if (!status.ok() && !status.IsNotFound()) {
        // FAIL
        std::cout << "Fail to get key: " << key << ", because: " << status.ToString() << std::endl;
        exit(1);
      }
      auto end = std::chrono::steady_clock::now();
      duration_in_nano = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    }
    logger->Log({.op = op, .operand = duration_in_nano});
  }
}

struct BenchmarkListener : public rocksdb::EventListener {
  std::string compaction_style;
  Logger<SimpleLog>* logger;
  rocksdb::AdaptiveCompactionController* comp_ctrl;
  int num_levels;

  int get_current_run_number(rocksdb::DB* db) {
    if (compaction_style == "dynamic") {
      return comp_ctrl->compactioner->latest_run_num.load();
    }
    int total_runs = 0;
    for (int i = 0; i < num_levels; i++) {
      std::string val;
      db->GetProperty("rocksdb.num-files-at-level" + std::to_string(i), &val);
      total_runs = (i == 0) ? total_runs + std::stoi(val) : total_runs + 1;
    }
    return total_runs;
  }

  void OnCompactionCompleted(rocksdb::DB* db, const rocksdb::CompactionJobInfo& cj) override {
    logger->Log({.op = "COMPACTION_COMPLETE_TO", .operand = cj.output_level });
    logger->Log({.op = "COMPACTION_RUN_NUMS", .operand = get_current_run_number(db)});
  }

  void OnFlushCompleted(rocksdb::DB* db, const rocksdb::FlushJobInfo& fj) override {
    logger->Log({.op = "FLUSH_COMPLETE_AT", .operand = fj.triggered_writes_slowdown });
    logger->Log({.op = "FLUSH_RUN_NUMS", .operand = get_current_run_number(db)});
  }

  BenchmarkListener(const std::string& comp_style, Logger<SimpleLog>* lg, decltype(comp_ctrl) cc, int numl):
    compaction_style(comp_style),
    logger(lg),
    comp_ctrl(cc),
    num_levels(numl) {}
};

rocksdb::Options GetBasedOptions() {
  rocksdb::Options opt;
  opt.statistics = rocksdb::CreateDBStatistics();
  opt.write_buffer_size = FLAGS_buffer_size;
  opt.create_if_missing = true;
  opt.compression = rocksdb::kNoCompression;
  auto table_options = opt.table_factory->GetOptions<rocksdb::BlockBasedTableOptions>();
  table_options->filter_policy.reset(rocksdb::NewBloomFilterPolicy(10, false));

  return opt;
}

rocksdb::Options GetDynamicOptions() {
  rocksdb::Options opt = GetBasedOptions();
  opt.compaction_style = rocksdb::kCompactionStyleDynamic;
  opt.delayed_write_rate = 0;
  opt.num_levels = 4;
  opt.level0_stop_writes_trigger = 0x7fffffff;
  opt.level0_slowdown_writes_trigger = 0x7fffffff;
  
  opt.comp_controller = new rocksdb::AdaptiveCompactionController(
    FLAGS_buffer_size, FLAGS_simulation_iter,
    0, FLAGS_write_ratio, FLAGS_read_ratio,
    FLAGS_parallel, 1000, FLAGS_key_size + FLAGS_value_size
  );
  return opt;
}

rocksdb::Options GetLevelingOptions() {
  rocksdb::Options opt = GetBasedOptions();
  opt.level_compaction_dynamic_level_bytes = false;
  return opt;
}


int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  Logger<SimpleLog>* logger = new Logger<SimpleLog>(FLAGS_output_file);
  WorkloadGenerator gen(FLAGS_benchmark_file, FLAGS_read_ratio, FLAGS_write_ratio, FLAGS_test_size);
  ThreadPool pool(4);
  pool.enqueue([&] {logger->StartLogging();});
  pool.enqueue([&] {gen.Produce();});
  rocksdb::Options opt;

  if (FLAGS_compaction_style == "dynamic") {
    opt = GetDynamicOptions();
  } else {
    opt = GetLevelingOptions();
  }

  opt.listeners.emplace_back(new BenchmarkListener(FLAGS_compaction_style, logger, opt.comp_controller, opt.num_levels));

  rocksdb::DB* db;
  auto status = rocksdb::DB::Open(opt, FLAGS_db_path, &db);
  if (!status.ok()) {
    std::cout << "Fail to open db: " << status.ToString() << std::endl;
    exit(1);
  }

  RunBenchmark(gen, db, logger);
  
  gen.StopProduce();
  logger->EndLogging();
  while (logger->IsEnded());
  delete logger;
  if (opt.comp_controller) {
    opt.comp_controller->stop_agent();
  }
  std::cout << opt.statistics->ToString() << std::endl;
  db->Close();
  return 0;
}