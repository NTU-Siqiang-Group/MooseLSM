#pragma once

#include <vector>
#include <cstdint>
#include <list>
#include <thread>
#include <memory>
#include <algorithm>
#include <unordered_map>
#include <numeric>
#include <atomic>
#include <string>
#include <iostream>
#include <cmath>
#include <mutex>

namespace DynCompactionV4 {
struct DynAction {
  std::vector<std::vector<int>> removed_files = decltype(removed_files)(20, std::vector<int>(100, 0));
  int start_level = -1;
  int target_level = -1;
  double reward = 0;
  int estimate_finished_idx = -1;
  double compaction_size = 0;

  std::string ToString() const {
    std::string ret;
    ret += "start_level: " + std::to_string(start_level) + ", target_level: "
      + std::to_string(target_level) + ", reward: " + std::to_string(reward) + ", compaction_size: "
      + std::to_string(compaction_size) + ", estimate_finished_idx: " + std::to_string(estimate_finished_idx) + "\n";
    for (int i = 0; i < (int)removed_files.size(); i++) {
      bool has_removed = false;
      std::string tmp = "Compact@" + std::to_string(i) + ": ";
      for (int j = 0; j < (int)removed_files[i].size(); j++) {
        if (removed_files[i][j]) {
          tmp += std::to_string(j) + " ";
          has_removed = true;
        }
      }
      if (has_removed) {
        ret += tmp + "\n";
      }
    }
    return ret;
  }
  void setRemovedFiles(int level, int start_idx, int end_idx, int val=1) {
    for (int i = start_idx; i <= end_idx; i++) {
      removed_files[level][i] = val;
    }
  }
};

struct TreeState {
  std::vector<std::vector<int64_t>> level_runs;
  int total_runs = 0;
  int max_level_runs = 0;

  std::vector<DynAction> actions;
  std::string ToString() const {
    std::string ret;
    ret += "total_runs: " + std::to_string(total_runs) + ", max_level_runs: " + std::to_string(max_level_runs) + "\n";
    for (int i = 0; i < (int)level_runs.size(); i++) {
      ret += "level " + std::to_string(i) + ": ";
      int64_t level_size = 0;
      for (int j = 0; j < (int)level_runs[i].size(); j++) {
        level_size += level_runs[i][j];
      }
      ret += "size: " + std::to_string(level_size) + ", runs: " + std::to_string(level_runs[i].size());
      ret += "\n";
    }
    // for (auto& action : actions) {
    //   ret += "action: start_level: " + std::to_string(action.start_level) 
    //     + ", target_level: " + std::to_string(action.target_level) 
    //     + ", reward: " + std::to_string(action.reward) 
    //     + ", compaction_size: " + std::to_string(action.compaction_size) + 
    //     + ", estimate idx: " + std::to_string(action.estimate_finished_idx) + "\n";
    // }
    ret += "--------------------------------\n";
    return ret;
  }
  void EnumerateActions() {
    DynAction major_compaction, empty;
    for (int i = 0; i < (int)level_runs.size(); i++) {
      empty.removed_files[i].resize(level_runs[i].size(), 0);
      major_compaction.removed_files[i].resize(level_runs[i].size(), 0);
    }
    major_compaction.start_level = 0;
    major_compaction.setRemovedFiles(0, 0, level_runs[0].size() - 1);
    major_compaction.compaction_size = std::accumulate(level_runs[0].begin(), level_runs[0].end(), 0L);
    major_compaction.reward = (int)level_runs[0].size() - 1;
    std::vector<int64_t> level_sizes(level_runs.size(), 0);
    for (int i = 0; i < (int)level_runs.size(); i++) {
      int64_t level_size = 0;
      if (level_runs[i].size() == 0) {
        continue;
      }
      DynAction compact_to_cur_level = empty;
      compact_to_cur_level.reward = -1;

      auto cur_level_major = major_compaction;
      for (int j = (int)level_runs[i].size() - 1; j >= 0; j--) {
        level_size += level_runs[i][j];

        if (j != 0) {
          compact_to_cur_level.removed_files[i][j] = 1;
          compact_to_cur_level.start_level = i;
          compact_to_cur_level.target_level = i;
          if (compact_to_cur_level.start_level == 0) {
            compact_to_cur_level.target_level = 1;
          }
          compact_to_cur_level.compaction_size = level_size;
          compact_to_cur_level.reward ++;
          actions.push_back(compact_to_cur_level);
        }
        
        if (i > 0) {
          cur_level_major.target_level = i;
          cur_level_major.removed_files[i][j] = 1;
          cur_level_major.compaction_size += level_runs[i][j];
          cur_level_major.reward ++;
          if (j == 0 && i + 1 < (int)level_runs.size()) {
            cur_level_major.target_level = i + 1;
          }
          actions.push_back(cur_level_major);
        } 
      }
      major_compaction = cur_level_major;
      
      auto compact_to_next = compact_to_cur_level;
      compact_to_next.start_level = i;
      compact_to_next.target_level = i + 1 < (int)level_runs.size() ? i + 1 : i;
      compact_to_next.reward = level_runs[i].size() - 1;
      compact_to_next.removed_files[i][0] = 1;
      compact_to_next.compaction_size = level_size;
      actions.push_back(compact_to_next);

      if (i + 1 < (int)level_runs.size() && level_runs[i + 1].size() > 0) {
        auto compact_with_next = compact_to_next;
        compact_with_next.start_level = i;
        compact_with_next.compaction_size += level_runs[i + 1].back(); // with the smallest
        compact_with_next.removed_files[i + 1].back() = 1;
        compact_with_next.reward = level_runs[i].size();
        actions.push_back(compact_with_next);
      }
    }
    int rw_ratio = 2;
    for (auto& action : actions) {
      action.compaction_size *= rw_ratio / 4096.0;
    }
  }
};

struct DynamicCompactionerV4 {
 private:
  std::mutex mtx;
  int triggered_compaction_nums = 0;
  int total_run_nums = 0;
  double prev_avg_sorted_runs = 1;
 public:
  constexpr static double kStoppedCost = 1e10;
  constexpr static double kStallCost = 4;
  // [r,u,p]
  std::tuple<int, int, int> workload{0, 2048, 0};

  TreeState most_recent_state;

  std::pair<int, int> Mc{100, 2000};

  int64_t buffer_size = 2L * (1<<20);

  double wait_io = 0;

  double parallel_factor = 1;

  static void get_win_acc_ios(int cur_total_runs, int max_offset, std::vector<double>& acc_ios, int64_t buffer_size, int write_stall, int r, int u, int p, double wait_io, double parallel_factor);
  static void get_reward_for_action(DynAction& action, int total_runs, const std::vector<double>& acc_ios, int c, int M, int r, int u, int p, int buffer_size, double wait_io, double parallel_factor);
  
  DynAction GetBestAction(TreeState& cur_state) {
    set_most_recent_state(cur_state);
    inc_triggered_comp(cur_state);
    if (cur_state.max_level_runs == 0 || cur_state.level_runs.size() == 0) {
      return DynAction(); // do nothing
    }
    DynAction best_action;
    auto mc = get_Mc();
    auto w = get_workload();
    auto [r, u, p] = w;
    int M = mc.first, c = mc.second;
    std::vector<double> acc_ios;
    get_win_acc_ios(cur_state.total_runs, 500, acc_ios, buffer_size, c, r, u, p, wait_io, parallel_factor);
    for (auto& action : cur_state.actions) {
      get_reward_for_action(action, cur_state.total_runs, acc_ios, c, M, r, u, p, buffer_size, wait_io, parallel_factor);
      if (action.reward > best_action.reward) {
        best_action = action;
      }
    }
    return best_action;
  }

  void set_most_recent_state(const TreeState& state) {
    std::lock_guard<std::mutex> guard(mtx);
    most_recent_state = state;
  }

  TreeState get_most_recent_state() {
    std::lock_guard<std::mutex> guard(mtx);
    auto ret = most_recent_state;
    return ret;
  }

  void set_workload(const std::tuple<int, int, int>& w) {
    std::lock_guard<std::mutex> guard(mtx);
    workload = w;
  }

  void set_Mc(const std::pair<int, int>& mc) {
    std::lock_guard<std::mutex> guard(mtx);
    Mc = mc;
  }

  std::tuple<int, int, int> get_workload() {
    std::lock_guard<std::mutex> guard(mtx);
    auto ret = workload;
    return ret;
  }

  std::pair<int, int> get_Mc() {
    std::lock_guard<std::mutex> guard(mtx);
    auto ret = Mc;
    return ret;
  }

  void inc_triggered_comp(const TreeState& state) {
    std::lock_guard<std::mutex> guard(mtx);
    triggered_compaction_nums ++;
    total_run_nums += state.total_runs;
  }

  bool need_reset_Mc() {
    std::lock_guard<std::mutex> guard(mtx);
    if (triggered_compaction_nums < 10) {
      return false;
    }
    double cur_avg_sorted_runs = 1.0 * total_run_nums / triggered_compaction_nums;
    if (std::abs(cur_avg_sorted_runs - prev_avg_sorted_runs) / prev_avg_sorted_runs > 0.1) {
      prev_avg_sorted_runs = cur_avg_sorted_runs;
      triggered_compaction_nums = 0;
      total_run_nums = 0;
      return true;
    }
    return false;
  }

  DynamicCompactionerV4(int64_t bf): buffer_size(bf) {}
};
} // namespace DynCompactionV4