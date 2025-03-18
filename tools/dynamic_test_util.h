#pragma once

#include "dynamic_lookforward.h"

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
  int prev_lookforward_max = 0;
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


  void StartProcessing(rocksdb::DB* db) {
    bool need_manual_compaction = db->GetOptions().compaction_style == rocksdb::kCompactionStyleDynamic;
    double r = 0, u = 0, p = 0;
    // if (need_manual_compaction) {
    //   r = workloads_[0]->total_range_lookup_cnt / (double)workloads_[0]->ops.size();
    //   u = workloads_[0]->total_update_cnt / (double)workloads_[0]->ops.size();
    //   p = workloads_[0]->total_point_lookup_cnt / (double)workloads_[0]->ops.size();
    //   auto recent_state = compaction_controller_->compactioner->most_recent_state;
    //   compaction_controller_->compactioner->lookforward = adaptive_lookforward_simulate(
    //     recent_state, buffer_size_, prev_lookforward_max, r, u, p, false
    //   );
    // }
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
      // if (need_manual_compaction && i + 1 < (int)workloads_.size()) {
      //   double new_r = workloads_[i + 1]->total_range_lookup_cnt / (double)workloads_[i + 1]->ops.size();
      //   double new_u = workloads_[i + 1]->total_update_cnt / (double)workloads_[i + 1]->ops.size();
      //   double new_p = workloads_[i + 1]->total_point_lookup_cnt / (double)workloads_[i + 1]->ops.size();
      //   bool fast_return = std::abs(new_r - r) / r <= 0.1 && std::abs(new_u - u) / u <= 0.1 && std::abs(new_p - p) / p <= 0.1;
      //   auto recent_state = compaction_controller_->compactioner->most_recent_state;
      //   auto start_time = std::chrono::high_resolution_clock::now();
      //   int new_lf = adaptive_lookforward_simulate(
      //     recent_state, buffer_size_, prev_lookforward_max, new_r, new_u, new_p, fast_return
      //   );
      //   auto end_time = std::chrono::high_resolution_clock::now();
      //   if (new_lf != 0) {
      //     std::cout << "New lookforward: " << new_lf 
      //       << ", used time: " << std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count() 
      //       << " us" << std::endl;
      //     compaction_controller_->compactioner->lookforward = new_lf;
      //   }
      // }
    }
  }

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