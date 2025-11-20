#pragma once

#include "dynamic_lookforward.h"

#include "dynamic_test_monitor.h"

#include <thread>
#include <chrono>
#include <iostream>
#include <random>
#include <fstream>
#include <sstream>
#include <queue>
#include <list>
#include "rocksdb/options.h"
#include "rocksdb/statistics.h"

const uint64_t MAX_KEY_VAL = 9999999999UL;
const uint64_t MIN_KEY_VAL = 0;

const int CPU_NUM = 32;

struct WorkloadManager {
  enum OpType : char {
    RANGE_LOOKUP = 0x0,
    UPDATE = 0x1,
    POINT_LOOKUP = 0x2,
    RANGE_LOOKUP_UPP = 0x3
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
  rocksdb::AdaptiveCompactionController* compaction_controller_;
  DynamicTestListener* test_monitor_;
  std::vector<int> record_times_;
  std::vector<OpType> record_ops_;
  std::vector<uint64_t> window_end_ts_;

  std::vector<std::vector<int>> continuous_workloads;
  bool use_continuous_workload = false;

  int64_t total_time;

  int sleep_time_ = 0;

  int mc_search_len_ = 400;

  const double kIOTime = 15;

  int parallel_factor = 1;
  std::mutex mtx;

  std::vector<int> new_workload_starts;
  std::vector<double> workload_stats;

  std::pair<std::string, std::string> split_string(const std::string& src, char delim = '-') {
    std::pair<std::string, std::string> ret;
    for (size_t i = 0; i < src.size(); ++i) {
      if (src[i] == delim) {
        ret.first = src.substr(0, i);
        ret.second = src.substr(i + 1);  // simpler and correct
        break;
      }
    }
    return ret;
  }

  void do_update(rocksdb::DB* db, const std::string& key, bool use_mtx=false) {
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
    if (!use_mtx) {
      record_times_.push_back(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
      record_ops_.push_back(OpType::UPDATE);
    } else {
      std::lock_guard<std::mutex> guard(mtx);
      record_times_.push_back(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
      record_ops_.push_back(OpType::UPDATE);
    }
  }

  void do_point_lookup(rocksdb::DB* db, const std::string& key, bool use_mtx=false) {
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
    if (!use_mtx) {
      record_times_.push_back(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
      record_ops_.push_back(OpType::POINT_LOOKUP);
    } else {
      std::lock_guard<std::mutex> guard(mtx);
      record_times_.push_back(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
      record_ops_.push_back(OpType::POINT_LOOKUP);
    }
    return;
  }

  void do_range_lookup(rocksdb::DB* db, const std::string& start_key, int len, bool use_mtx=false) {
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
    if (!use_mtx) {
      record_times_.push_back(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
      record_ops_.push_back(OpType::RANGE_LOOKUP);
    } else {
      std::lock_guard<std::mutex> guard(mtx);
      record_times_.push_back(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
      record_ops_.push_back(OpType::RANGE_LOOKUP);
    }
    delete it;
    return;
  }

  void do_upper_range_lookup(rocksdb::DB* db, const std::string& start_key, const std::string& end_key, bool use_mtx=false) {
    auto key = start_key;
    PadStringWithPrefix(key_size_, key);
    auto ek = end_key;
    PadStringWithPrefix(key_size_, ek);
    rocksdb::Slice endk(ek);
    auto ropt = rocksdb::ReadOptions();
    ropt.iterate_upper_bound = &endk;
    auto it = db->NewIterator(ropt);
    std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
    it->Seek(key);
    for (int i = 0;; i++) {
      if (!it->Valid()) {
        break;
      }
      if (it->key() == ek) {
        break;
      }
      it->Next();
    }
    std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
    if (!use_mtx) {
      record_times_.push_back(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
      record_ops_.push_back(OpType::RANGE_LOOKUP);
    } else {
      std::lock_guard<std::mutex> guard(mtx);
      record_times_.push_back(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
      record_ops_.push_back(OpType::RANGE_LOOKUP);
    }
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
      } else if (window->ops[window->cur_op_idx] == OpType::POINT_LOOKUP) {
        // point lookup
        auto key = window->keys[window->cur_op_idx];
        this->do_point_lookup(db, key);
      } else if (window->ops[window->cur_op_idx] == OpType::RANGE_LOOKUP_UPP) {
        auto keys = split_string(window->keys[window->cur_op_idx]);
        this->do_upper_range_lookup(db, keys.first, keys.second); 
      }
      if (sleep_time_ > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(sleep_time_));
      }
      window->cur_op_idx++;
    }
  }

  void ProcessWindowInParallel(rocksdb::DB* db, const std::shared_ptr<WorkloadWindow>& window) {
    int length = (int)window->ops.size();
    omp_set_num_threads(parallel_factor);
    # pragma omp parallel for
    for (int i = 0; i < length; i++) {
      if (window->ops[i] == OpType::RANGE_LOOKUP) {
        auto key = window->keys[i];
        this->do_range_lookup(db, key, this->range_lookup_len_, true);
      } else if (window->ops[i] == OpType::UPDATE) {
        // do update
        auto key = window->keys[i];
        this->do_update(db, key, true);
      } else if (window->ops[i] == OpType::POINT_LOOKUP) {
        // point lookup
        auto key = window->keys[i];
        this->do_point_lookup(db, key, true);
      } else if (window->ops[i] == OpType::RANGE_LOOKUP_UPP) {
        auto keys = split_string(window->keys[i]);
        this->do_upper_range_lookup(db, keys.first, keys.second, true); 
      }
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
      } else if (tokens[0] == "UPDATE") {
        window->ops.push_back(OpType::UPDATE);
        window->keys.push_back("2" + tokens[1]);
        update_cnt++;
      } else if (tokens[0] == "READ") {
        window->ops.push_back(OpType::POINT_LOOKUP);
        window->keys.push_back(tokens[1]);
        point_lookup_cnt++;
        point_lookup_sums ++;
      } else if (tokens[0] == "SCANUP") {
        window->ops.push_back(OpType::RANGE_LOOKUP_UPP);
        window->keys.push_back(tokens[1]);
        range_lookup_cnt ++;
        range_lookup_sums ++;
      }
      if (update_cnt >= (int)buffer_size_ / (key_size_ + value_size_)) {
        window->cur_op_idx = 0;
        window->total_range_lookup_cnt = range_lookup_cnt;
        window->total_update_cnt = update_cnt;
        window->total_point_lookup_cnt = point_lookup_cnt;
        workloads_.push_back(window);

        window = std::make_shared<WorkloadWindow>();
        update_cnt = 0;
        range_lookup_cnt = 0;
        point_lookup_cnt = 0;
      }
    }
    if (window->ops.size() > 0) {
      window->cur_op_idx = 0;
      window->total_range_lookup_cnt = range_lookup_cnt;
      window->total_update_cnt = update_cnt;
      window->total_point_lookup_cnt = point_lookup_cnt;
      workloads_.push_back(window);
    }
    new_workload_starts.push_back(0);
    for (int i = 1; i < (int)workloads_.size(); i++) {
      double r = 1.0 * workloads_[i]->total_range_lookup_cnt / workloads_[i]->ops.size();
      double u = 1.0 * workloads_[i]->total_update_cnt / workloads_[i]->ops.size();
      double p = 1.0 * workloads_[i]->total_point_lookup_cnt / workloads_[i]->ops.size();
      double prev_r = 1.0 * workloads_[i - 1]->total_range_lookup_cnt / workloads_[i - 1]->ops.size();
      double prev_u = 1.0 * workloads_[i - 1]->total_update_cnt / workloads_[i - 1]->ops.size();
      double prev_p = 1.0 * workloads_[i - 1]->total_point_lookup_cnt / workloads_[i - 1]->ops.size();

      // change over 10%
      if (r - prev_r > 0.05 || u - prev_u > 0.05 || p - prev_p > 0.05) {
        new_workload_starts.push_back(i);
      }
    }
    // int ops = 0;
    // for (int i = 0; i < (int)workloads_.size(); i++) {
    //   ops += workloads_[i]->ops.size();
    //   if (ops >= 1024000 * 4) {
    //     new_workload_starts.push_back(i);
    //     ops = 0;
    //   }
    // }
    // workload_stats = {0.9, 0.66, 0.43, 0.24, 0.11, 0.04, 0.03, 0.09, 0.21, 0.38, 0.6, 0.84};
    if (use_continuous_workload) {
      uint64_t total_ops = 0, rops = 0, pops = 0, uops = 0;
      int start_win = 0;
      int base = buffer_size_ / (key_size_ + value_size_);
      for (int i = 0; i < (int)workloads_.size(); i++) {
        rops += workloads_[i]->total_range_lookup_cnt;
        uops += workloads_[i]->total_update_cnt;
        pops += workloads_[i]->total_point_lookup_cnt;
        total_ops += workloads_[i]->total_point_lookup_cnt + workloads_[i]->total_range_lookup_cnt + workloads_[i]->total_update_cnt;
        if (total_ops >= 2000000) {
          int r = base * rops / uops;
          int p = base * pops / uops;
          int u = base;
          continuous_workloads.push_back(
            {start_win, r, u, p}
          );
          start_win = i + 1;
          rops = 0;
          uops = 0;
          pops = 0;
          total_ops = 0;
        }
      }
      if (total_ops > 0) {
        int r = base * rops / uops;
        int p = base * pops / uops;
        int u = base;
        continuous_workloads.push_back(
          {start_win, r, u, p}
        );
      }
    }

    f.close();
  }


  void StartProcessing(rocksdb::DB* db) {
    auto start_time_point = std::chrono::high_resolution_clock::now();
    if (test_monitor_ != nullptr) {
      test_monitor_->SetStartPoint(start_time_point);
    }
    std::cout << "Total entry: " << get_db_size(db) << std::endl;
    bool need_manual_compaction = db->GetOptions().compaction_style == rocksdb::kCompactionStyleDynamic;
    print_config();
    double r = 0, u = 0, p = 0;
    double wait_io = sleep_time_ * 1.0 / kIOTime;
    auto compactioner = compaction_controller_->compactioner;
    if (need_manual_compaction) {
      compactioner->wait_io = wait_io;
      r = workloads_[0]->total_range_lookup_cnt;
      u = workloads_[0]->total_update_cnt;
      p = workloads_[0]->total_point_lookup_cnt;
      // r = 2048.0 / (1 - workload_stats[0]) * workload_stats[0];
      // u = 2048;
      // p = 0;
      compactioner->set_workload(
        {r, u, p}
      );
      compactioner->parallel_factor = parallel_factor;
      auto latest_state = compactioner->get_most_recent_state();

      int remaining_window_cnt = 0;
      auto it = std::upper_bound(new_workload_starts.begin(), new_workload_starts.end(), 0);
      if (it == new_workload_starts.end()) {
        remaining_window_cnt = (int)workloads_.size();
      } else {
        remaining_window_cnt = *it;
      }
      auto [m, c] = FindBestMC(latest_state, buffer_size_, r, u, p, wait_io, mc_search_len_, remaining_window_cnt, parallel_factor);
      std::cout << "Window 0" << " : new M: " << m << ", new C: " << c 
            << ", (r,u,p): " << r << ", " << u << ", " << p
            << ", current total run: " << latest_state.total_runs << ", remaining_window_cnt: " << remaining_window_cnt << std::endl;
      compactioner->set_Mc({m, c});
    }
    int64_t mc_threshold = 0;
    int64_t reclaim_threshold = 0;
    int64_t total_ops = 0;
    for (int i = 0; i < (int)workloads_.size(); i++) {
      std::cout << "window #" << i << ", range lookup cnt: " << workloads_[i]->total_range_lookup_cnt
        << ", update cnt: " << workloads_[i]->total_update_cnt
        << ", point lookup cnt: " << workloads_[i]->total_point_lookup_cnt
        << ", acc avg: " << get_acc_avg() << ", #runs: " << record_run_nums(db) << std::endl;
      std::cout << "range filter use: " << db->GetOptions().statistics->getTickerCount(rocksdb::RANGE_FILTER_USE) 
        << ", range filter skip: " << db->GetOptions().statistics->getTickerCount(rocksdb::RANGE_FILTER_SKIP) << std::endl;
      compaction_controller_->cur_win_num.store(i);
      auto start = std::chrono::high_resolution_clock::now();
      if (parallel_factor == 1) {
        auto win_startts = std::chrono::high_resolution_clock::now();
        // double est_cost = get_estimate_cost(
        //   workloads_[i]->total_range_lookup_cnt,
        //   workloads_[i]->total_update_cnt,
        //   workloads_[i]->total_point_lookup_cnt
        // );
        ProcessWindow(db, workloads_[i]);
        auto win_endts = std::chrono::high_resolution_clock::now();
        // std::cout << "window #" << i << ", est cost: " << est_cost << ", real cost: "
        //   << (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(win_endts - win_startts).count()
        //   << std::endl;
        auto end_ts = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - start_time_point).count();
        window_end_ts_.push_back(end_ts);
      } else {
        ProcessWindowInParallel(db, workloads_[i]);
      }
      // ProcessWindow(db, workloads_[i]);
      auto end = std::chrono::high_resolution_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
      total_time += duration;
      mc_threshold += workloads_[i]->ops.size();
      reclaim_threshold += workloads_[i]->ops.size();
      total_ops += workloads_[i]->ops.size();
      if (need_manual_compaction && i + 1 < (int)workloads_.size()) {
        r = workloads_[i + 1]->total_range_lookup_cnt;
        u = workloads_[i + 1]->total_update_cnt;
        p = workloads_[i + 1]->total_point_lookup_cnt;
        int continuous_remaining = 0;
        // auto it = std::upper_bound(new_workload_starts.begin(), new_workload_starts.end(), i + 1);
        // if (it == new_workload_starts.end()) {
        //   r = 2048.0 / (1 - workload_stats[workload_stats.size() - 1]) * workload_stats[workload_stats.size() - 1];
        //   u = 2048;
        //   p = 0;
        // } else {
        //   int idx = it - new_workload_starts.begin() - 1;
        //   // std::cout << "idx: " << idx << std::endl;
        //   r = 2048.0 / (1 - workload_stats[idx]) * workload_stats[idx];
        //   u = 2048;
        //   p = 0;
        // }
        if (use_continuous_workload) {
          int cur_idx = 0;
          for (int j = 1; j < continuous_workloads.size(); j ++) {
            if (i + 1 < continuous_workloads[j][0]) {
              cur_idx = j - 1;
              continuous_remaining = continuous_workloads[j][0] - i;
              break;
            }
            cur_idx = j;
          }
          if (cur_idx == continuous_workloads.size() - 1) {
            continuous_remaining = workloads_.size() - i;
          }
          r = continuous_workloads[cur_idx][1];
          u = continuous_workloads[cur_idx][2];
          p = continuous_workloads[cur_idx][3];
        }
        compactioner->set_workload(
          { r, u, p }
        );
        if (compactioner->need_reset_Mc() || mc_threshold >= 2048000) {
          auto latest_state = compactioner->get_most_recent_state();
          auto start = std::chrono::high_resolution_clock::now();
          int remaining_window_cnt = 0;

          auto it = std::upper_bound(new_workload_starts.begin(), new_workload_starts.end(), i + 1);

          if (it == new_workload_starts.end()) {
            remaining_window_cnt = (int)workloads_.size() - (i + 1);
          } else {
            remaining_window_cnt = *it - (i + 1);
          }

          if (use_continuous_workload) {
            remaining_window_cnt = continuous_remaining;
          }

          auto [m, c] = FindBestMC(latest_state, buffer_size_, r, u, p, wait_io, mc_search_len_, remaining_window_cnt, parallel_factor);
          if (m == 0 || c == 0) {
            // invalid case
            std::cout << "[WARNING] invalid MC: " << remaining_window_cnt << std::endl;
          }
          auto end = std::chrono::high_resolution_clock::now();
          std::cout << "FindBestMC time: " << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() << std::endl;
          // // set the new M and C
          if ((double)p > 0.9 * (r + u + p)) {
            m = 5;
            c = 1000000;
          }
          compactioner->set_Mc({m, c});
          std::cout << "Window " << i << " : new M: " << m << ", new C: " << c 
            << ", (r,u,p): " << r << ", " << u << ", " << p
            << ", current total run: " << latest_state.total_runs << ", remaining_window_cnt: " << remaining_window_cnt << std::endl;
          mc_threshold = 0;
        }
        if (reclaim_threshold >= 10240000) {
          reclaim_frag();
          reclaim_threshold = 0;
          std::cout << "Current Avg: " << get_acc_avg() << std::endl;
        }
      }
    }
    // std::cout << "Total entry: " << get_db_size(db) << std::endl;
  }
  double get_acc_avg() {
    return std::accumulate(record_times_.begin(), record_times_.end(), 0UL) * 1.0 / record_times_.size();
  }

  double get_estimate_cost(int r, int u, int p) {
    auto [M, c] = compaction_controller_->compactioner->get_Mc();
    int runs = compaction_controller_->compactioner->get_most_recent_state().total_runs;
    double cost = runs * (r + 0.01 * p) + p;
    if (runs > c) {
      cost += u * 0.4;
    }
    return cost;
  }

  void print_config() {
    std::cout << "key size: " << key_size_
      << ", value size: " << value_size_
      << ", range lookup len: " << range_lookup_len_
      << ", buffer size: " << buffer_size_ << std::endl
      << "sleep time: " << sleep_time_
      << ", mc search len: " << mc_search_len_ << std::endl
      << "workload size: " << workloads_.size() << std::endl;

    for (auto idx : new_workload_starts) {
      std::cout << "new workload start at: " << idx << std::endl;
    }
  }

  uint64_t get_db_size(rocksdb::DB* db) {
    std::string num_keys;
    db->GetProperty("rocksdb.estimate-num-keys", &num_keys);
    int num = std::stoi(num_keys);
    return num;
  }

  void checkpoint() {
    std::string cmd = "rm -rf /tmp/checkpoint && cp -r /tmp/db /tmp/checkpoint";
    int ret = system(cmd.c_str());
    if (ret != 0) {
      std::cout << "fail to checkpoint: " << ret << std::endl;
    }
    std::cout << "Finish checkpoint" << std::endl;
  }
  void reclaim_frag() {
    std::cout << "Trimming..." << std::endl;
    std::string sudo_passwd = "WODEMAYA1234(qqtt)";
    std::string cmd = "echo \"" + sudo_passwd + "\" | sudo -S fstrim -v /tmp";
    int ret = system(cmd.c_str());
    if (ret != 0) {
      std::cout << "fail to trim: " << ret << std::endl;
    }
    std::cout << "Finish trimming" << std::endl;
  }

  uint32_t record_run_nums(rocksdb::DB* db) {
    auto compaction_style = db->GetOptions().compaction_style;
    if (compaction_style == rocksdb::CompactionStyle::kCompactionStyleDynamic) {
      auto compactioner = compaction_controller_->compactioner;
      return compactioner->latest_run_num.load();
    } else if (compaction_style == rocksdb::CompactionStyle::kCompactionStyleLevel) {
      std::string value;
      db->GetProperty("rocksdb.num-files-at-level0", &value);
      return std::stoi(value) + db->GetOptions().num_levels - 1;
    } else if (compaction_style == rocksdb::CompactionStyle::kCompactionStyleMoose) {
      return compaction_controller_->latest_run_num.load();
    }
    return 0;
  }

  WorkloadManager(
    rocksdb::AdaptiveCompactionController* comp,
    DynamicTestListener* listener, 
    int key_size = 24,
    int value_size = 1000,
    int range_lookup_len = 16,
    uint64_t buffer_size = 2 * (1<<20),
    int sleep_time = 0,
    int mc_search_len = 400,
    int para_f = 1,
    bool use_continuous = false
  ) :
    key_size_(key_size),
    value_size_(value_size),
    range_lookup_len_(range_lookup_len),
    buffer_size_(buffer_size),
    compaction_controller_(comp),
    test_monitor_(listener),
    sleep_time_(sleep_time),
    mc_search_len_(mc_search_len),
    parallel_factor(para_f),
    use_continuous_workload(use_continuous)
  {}
};