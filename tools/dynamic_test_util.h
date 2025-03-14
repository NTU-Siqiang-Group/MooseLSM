#pragma once

#include "rocksdb/options.h"
#include "rocksdb/experimental.h"

#include "dynamic_thread_pool.h"
#include "dynamic_test_monitor.h"

#include <thread>
#include <chrono>
#include <iostream>
#include <random>
#include <fstream>
#include <sstream>
#include <queue>
#include <list>

const uint64_t MAX_KEY_VAL = 9999999999UL;
const uint64_t MIN_KEY_VAL = 0;

const int CPU_NUM = 32;

struct WorkloadManager {
  enum OpType : char {
    RANGE_LOOKUP = 0x0,
    UPDATE = 0x1,
    POINT_LOOKUP = 0x2
  };

  struct WorkloadWindow {
    int total_range_lookup_cnt;
    int total_update_cnt;
    int total_point_lookup_cnt;
    std::vector<OpType> ops;
    std::vector<std::string> keys;
    int cur_op_idx;

    int intensity; // in us
  };

  // ThreadPool pool;

  std::vector<std::shared_ptr<WorkloadWindow>> workloads_;
  int key_size_;
  int value_size_;
  int range_lookup_len_;
  uint64_t buffer_size_;
  rocksdb::AtomicCompactionController* compaction_controller_;
  DynamicTestLogger* test_monitor_;
  std::vector<int> record_times_;
  std::vector<OpType> record_ops_;

  void do_update(rocksdb::DB* db, const std::string& key) {
    auto value_str = key;
    PadStringWithPrefix(value_size_, value_str);
    auto key_str = key;
    PadStringWithPrefix(key_size_, key_str);
    std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
    auto s = db->Put(rocksdb::WriteOptions(), key_str, value_str);
    if (!s.ok()) {
      std::cout << "fail to put key: " << s.ToString() << std::endl;
      exit(1);
    }
    std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
    record_times_.push_back(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
    record_ops_.push_back(OpType::UPDATE);
  }

  void do_point_lookup(rocksdb::DB* db, const std::string& key) {
    auto query_key = key;
    std::string val;
    PadStringWithPrefix(key_size_, query_key);
    std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
    auto s = db->Get(rocksdb::ReadOptions(), query_key, &val);
    if (!s.ok()) {
      std::cout << "fail to get key " << key << ": " << s.ToString() << std::endl;
      exit(1);
    }
    std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
    record_times_.push_back(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
    record_ops_.push_back(OpType::POINT_LOOKUP);
    return;
  }

  void do_range_lookup(rocksdb::DB* db, const std::string& start_key, int len) {
    auto key = start_key;
    PadStringWithPrefix(key_size_, key);
    auto it = db->NewIterator(rocksdb::ReadOptions());
    std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
    it->Seek(key);
    for (int i = 0; i < len; i++) {
      if (!it->Valid()) {
        break;
      }
      it->Next();
    }
    std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
    record_times_.push_back(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
    record_ops_.push_back(OpType::RANGE_LOOKUP);
    // test_monitor_->Log(MonitorOpType::RANGE_LOOKUP, std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()));
    delete it;
    return;
  }

  void ProcessWindow(rocksdb::DB* db, const std::shared_ptr<WorkloadWindow>& window) {
    while (window->cur_op_idx < (int)window->ops.size()) {
      if (window->ops[window->cur_op_idx] == OpType::RANGE_LOOKUP) {
        auto key = window->keys[window->cur_op_idx];
        this->do_range_lookup(db, key, this->range_lookup_len_);
      } else if (window->ops[window->cur_op_idx] == OpType::UPDATE) {
        // do update
        auto key = window->keys[window->cur_op_idx];
        this->do_update(db, key);
      } else {
        // point lookup
        auto key = window->keys[window->cur_op_idx];
        this->do_point_lookup(db, key);
      }
      window->cur_op_idx++;
    }
  }

  static void PadStringWithPrefix(int length, std::string& str) {
    if ((int)str.size() < length) {
      str.insert(0, length - str.size(), '0');
    }
  }
 
 public:
  void InitWorkloadFromFile(const std::string& filename, bool repeat=true) {
    std::ifstream f(filename);
    std::string line;
    auto window = std::make_shared<WorkloadWindow>();
    int update_cnt = 0;
    int range_lookup_cnt = 0, point_lookup_cnt = 0;
    uint64_t range_lookup_sums = 0, point_lookup_sums = 0;
    while (std::getline(f, line)) {
      // split line with space
      std::istringstream iss(line);
      std::vector<std::string> tokens(2);
      iss >> tokens[0] >> tokens[1];
      if (tokens[0] == "SCAN") {
        window->ops.push_back(OpType::RANGE_LOOKUP);
        window->keys.push_back(tokens[1]);
        range_lookup_cnt++;
        range_lookup_sums ++;
      } else if (tokens[0] == "INSERT") {
        window->ops.push_back(OpType::UPDATE);
        window->keys.push_back(tokens[1]);
        update_cnt++;
      } else if (tokens[0] == "READ") {
        window->ops.push_back(OpType::POINT_LOOKUP);
        window->keys.push_back(tokens[1]);
        point_lookup_cnt++;
        point_lookup_sums ++;
      }
      if (update_cnt >= (int)buffer_size_ / (key_size_ + value_size_)) {
        window->cur_op_idx = 0;
        window->total_range_lookup_cnt = range_lookup_cnt;
        window->total_update_cnt = update_cnt;
        window->total_point_lookup_cnt = point_lookup_cnt;
        workloads_.push_back(window);

        DynCompactionV3::SearchNode node;
        node.range_lookup_nums = range_lookup_cnt;
        node.point_lookup_nums = point_lookup_cnt;
        node.update_nums = update_cnt;
        node.prefix_sum_range_lookups = range_lookup_sums;
        node.prefix_sum_point_lookups = point_lookup_sums;
        compaction_controller_->compactioner->workload.Append(node);

        window = std::make_shared<WorkloadWindow>();
        update_cnt = 0;
        range_lookup_cnt = 0;
        point_lookup_cnt = 0;
      }
    }
    // repeat the last search node 500 times
    auto node = compaction_controller_->compactioner->workload.At(compaction_controller_->compactioner->workload.windows.size() - 1);
    for (int i = 0; repeat && i < 500; i++) {
      compaction_controller_->compactioner->workload.Append(node);
    }
    f.close();
  }

  int dynamic_lookforward(double u, double r, double p) {
    // return -720 * x * x + 720 * x + 20;
    // return 200;
    // return -600 * x * x + 600 * x + 50;
    // return 100 * x + 100;
    // double variance = 
    // normalize to [0, 1]
    // double sum = u + r + p;
    // u /= sum;
    // r /= sum;
    // p /= sum;
    // double mean = (u + r + p) / 3;
    // double variance = (u - mean) * (u - mean) + (r - mean) * (r - mean) + (p - mean) * (p - mean);
    // int lf = 100 + 150 * variance;
    // return lf;
    return 50;
  }

  int dynamic_lookforward(int done_ops) {
    // int idx = done_ops / 10240000;
    // static std::vector<int> lfs{200, 200, 150, 150, 50, 150, 100, 50, 50, 50, 50, 50, 50, 50, 50, 150, 150};
    // if (idx >= (int)lfs.size()) {
    //   return 150;
    // }
    // return lfs[idx];
    return 50;
  }
  
  // int dynamic_lookforward(int next_idx) {
  //   // collect 100000 ops
  //   int64_t ops = 0;
  //   double r = 0, u = 0, p = 0;
  //   for (int i = next_idx; ops < 1000000 && i < (int)workloads_.size(); i++) {
  //     ops += workloads_[i]->ops.size();
  //     r += workloads_[i]->total_range_lookup_cnt;
  //     u += workloads_[i]->total_update_cnt;
  //     p += workloads_[i]->total_point_lookup_cnt;
  //   }
  //   r /= ops;
  //   u /= ops;
  //   p /= ops;
  //   if (p > 0.9) {
  //     return 200;
  //   }
  //   if (r > 0.9) {
  //     return 300;
  //   }
  //   if (u > 0.9) {
  //     return 50;
  //   }
  //   if (u > 0.4) {
  //     return 150;
  //   }
  //   return 100;
  // }

  void StartProcessing(rocksdb::DB* db) {
    bool need_manual_compaction = db->GetOptions().compaction_style == rocksdb::kCompactionStyleDynamic;
    if (need_manual_compaction) {
      int lf = dynamic_lookforward(0);
      compaction_controller_->compactioner->lookforward = lf;
    }
    int64_t finished_ops = 0;
    for (int i = 0; i < (int)workloads_.size(); i++) {
      std::cout << "window #" << i << ", range lookup cnt: " << workloads_[i]->total_range_lookup_cnt
        << ", update cnt: " << workloads_[i]->total_update_cnt
        << ", point lookup cnt: " << workloads_[i]->total_point_lookup_cnt  << std::endl;
      ProcessWindow(db, workloads_[i]);
      finished_ops += workloads_[i]->ops.size();
      // if (need_manual_compaction) {
      compaction_controller_->cur_win_idx ++;
      // reset the search depth
      if (need_manual_compaction && i + 1 < (int)workloads_.size()) {
        // int lf = dynamic_lookforward(
        //   workloads_[i + 1]->total_update_cnt,
        //   workloads_[i + 1]->total_range_lookup_cnt,
        //   workloads_[i + 1]->total_point_lookup_cnt
        // );
        // compaction_controller_->compactioner->lookforward = lf;
        compaction_controller_->compactioner->lookforward = dynamic_lookforward(finished_ops);
        // compaction_controller_->compactioner->lookforward = dynamic_lookforward(i + 1);
      }
    }
  }

  // void DumpWorkloadToFile(const std::string& file) {
  //   std::ofstream f(file);
  //   for (int i = 0; i < (int)workloads_.size(); i++) {
  //     DumpWorkload(workloads_[i], f);
  //   }
  //   f.close();
  // }

  WorkloadManager(
    rocksdb::AtomicCompactionController* comp,
    DynamicTestLogger* logger, 
    int key_size = 24,
    int value_size = 1000,
    int range_lookup_len = 16,
    uint64_t buffer_size = 2 * (1<<20)
  ) :
    key_size_(key_size),
    value_size_(value_size),
    range_lookup_len_(range_lookup_len),
    buffer_size_(buffer_size),
    compaction_controller_(comp),
    test_monitor_(logger)
  {}
};