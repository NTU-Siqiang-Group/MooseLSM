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

namespace DynCompactionV3 {
struct SearchNode {
  int32_t range_lookup_nums = 0;
  int32_t update_nums = 0;
  int32_t point_lookup_nums = 0;

  int64_t prefix_sum_range_lookups = 0;
  int64_t prefix_sum_point_lookups = 0;

  int wait_micros = 0;
};

struct Sequence {
  std::vector<SearchNode> windows;

  SearchNode At(int idx) const {
    return windows[idx];
  }

  void Append(const SearchNode& node) {
    windows.push_back(node);
  }
};

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
    for (auto& action : actions) {
      ret += "action: start_level: " + std::to_string(action.start_level) 
        + ", target_level: " + std::to_string(action.target_level) 
        + ", reward: " + std::to_string(action.reward) 
        + ", compaction_size: " + std::to_string(action.compaction_size) + 
        + ", estimate idx: " + std::to_string(action.estimate_finished_idx) + "\n";
    }
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
      // for (int j = 0; j < (int)level_runs[i].size(); j++) {
      //   level_size += level_runs[i][j];
      //   if (j != level_runs[i].size() - 1) {
      //     compact_to_cur_level.removed_files[i][j] = true;
      //     compact_to_cur_level.start_level = i;
      //     compact_to_cur_level.target_level = i;
      //     compact_to_cur_level.compaction_size = level_size;
      //     compact_to_cur_level.reward = j; // compact j + 1 and create one new
      //     actions.push_back(compact_to_cur_level);
      //   }
      // }

      auto cur_level_major = major_compaction;
      for (int j = (int)level_runs[i].size() - 1; j >= 0; j--) {
        level_size += level_runs[i][j];

        if (j != 0) {
          compact_to_cur_level.removed_files[i][j] = 1;
          compact_to_cur_level.start_level = i;
          compact_to_cur_level.target_level = i;
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

struct DynamicCompactionerV3 {
  Sequence workload;
  int64_t buffer_size = 2 * (1<<20);
  std::atomic<int> lookforward{100};

  void get_win_acc_ios(int cur_total_runs, int start_win_idx, int max_offset,
    std::vector<double>& acc_ios, std::vector<double>& remain_rr, std::vector<double>& remain_p) {
    acc_ios.resize(max_offset, 0);
    remain_rr.resize(max_offset, 0);
    remain_p.resize(max_offset, 0);
    int cur_runs = cur_total_runs;
    double accio = 0;
    int64_t remain_rr_nums = std::accumulate(workload.windows.begin() + start_win_idx, workload.windows.begin() + start_win_idx + max_offset, 0L, [](int acc, const SearchNode& node) {
        return acc + node.range_lookup_nums;
      }),
      remain_p_nums = std::accumulate(workload.windows.begin() + start_win_idx, workload.windows.begin() + start_win_idx + max_offset, 0L, [](int acc, const SearchNode& node) {
        return acc + node.point_lookup_nums;
      });
    for (int i = 0; i < max_offset; i++) {
      auto window = workload.At(i + start_win_idx);
      remain_rr_nums -= window.range_lookup_nums;
      remain_p_nums -= window.point_lookup_nums;

      accio += window.range_lookup_nums * cur_runs + buffer_size / 4096.0 + window.point_lookup_nums * (0.01 * cur_runs + 1);
      cur_runs ++;
      
      acc_ios[i] = accio;
      remain_rr[i] = remain_rr_nums;
      remain_p[i] = remain_p_nums;
    }
  }

  // void adaptive_lookforward(const TreeState& cur_state) {
  //   int run_nums = cur_state.total_runs;
  //   int64_t total_size = 0;
  //   for (int i = 0; i < (int)cur_state.level_runs.size(); i++) {
  //     total_size += std::accumulate(cur_state.level_runs[i].begin(), cur_state.level_runs[i].end(), 0L);
  //   }
  //   int lf = run_nums * std::floor(std::log2(1.0 * total_size / buffer_size));
  //   lf = std::max(50, lf);
  //   lookforward = lf;
  // }

  DynAction GetBestAction(TreeState& cur_state, int start_win_idx) {
    if (cur_state.max_level_runs == 0 || workload.windows.size() == 0) {
      return DynAction(); // do nothing
    }
    DynAction best_action;
    std::vector<double> acc_ios, remain_rrs, remain_ps;
    get_win_acc_ios(cur_state.total_runs, start_win_idx, lookforward, acc_ios, remain_rrs, remain_ps);
    // get estimate finished idx for action
    for (auto& action : cur_state.actions) {
      // find first idx that acc_ios[idx] >= action.compaction_size
      int idx = 0;
      auto it = std::lower_bound(acc_ios.begin(), acc_ios.end(), action.compaction_size);
      idx = it - acc_ios.begin();
      if (it == acc_ios.end() || idx >= (int)acc_ios.size() - 1) {
        action.reward = 0;
        continue;
      }
      action.estimate_finished_idx = idx;
      double remain_rr = remain_rrs[idx], remain_p = remain_ps[idx];
      action.reward = (remain_rr + remain_p * 0.01) * action.reward;
      if (idx > 0) {
        double prev_rr = remain_rrs[0] - remain_rrs[idx - 1],
          prev_p = remain_ps[0] - remain_ps[idx - 1];
        double cost = prev_rr * idx / 2 + prev_p * 0.01 / 2;
        action.reward -= cost;
      }
      if (action.reward > best_action.reward) {
        best_action = action;
      }
    }
    return best_action;
  }

  DynAction GetBestActionV2(TreeState& cur_state, int start_win_idx) {
    if (cur_state.max_level_runs == 0 || workload.windows.size() == 0) {
      return DynAction(); // do nothing
    }
    DynAction best_action;
    std::vector<double> acc_ios, remain_rrs, remain_ps;
    get_win_acc_ios(cur_state.total_runs, start_win_idx, 1000, acc_ios, remain_rrs, remain_ps);
    for (auto& action : cur_state.actions) {
      // find first idx that acc_ios[idx] >= action.compaction_size
      int idx = 0;
      auto it = std::lower_bound(acc_ios.begin(), acc_ios.end(), action.compaction_size);
      idx = it - acc_ios.begin();
      if (it == acc_ios.end() || idx >= (int)acc_ios.size() - 1) {
        action.reward = 0;
        continue;
      }
      action.estimate_finished_idx = idx;
      int reduced_run = action.reward;
      double cost = 0;
      if (idx > 0) {
        double prev_rr = remain_rrs[0] - remain_rrs[idx - 1],
          prev_p = remain_ps[0] - remain_ps[idx - 1];
        cost = prev_rr * idx / 2 + prev_p * 0.01 * idx / 2;
      }
      double offset_cost = 0;
      int elapsed_win = 0;
      for (int i = idx + 1; offset_cost < cost && i < (int)remain_rrs.size(); i++) {
        int range_nums = workload.windows[start_win_idx + i].range_lookup_nums;
        int point_nums = workload.windows[start_win_idx + i].point_lookup_nums;
        offset_cost += range_nums * reduced_run + point_nums * 0.01 * reduced_run;
        elapsed_win ++;
      }
      action.reward = reduced_run - elapsed_win;
      if (action.reward > best_action.reward) {
        best_action = action;
      }
    }
    return best_action;
  }

  DynamicCompactionerV3(int64_t buffer_size, int lf=100): buffer_size(buffer_size), lookforward(lf) {}
};

} // namespace DynCompactionV3