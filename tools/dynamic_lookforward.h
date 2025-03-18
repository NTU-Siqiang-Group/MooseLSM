#include "rocksdb/options.h"
#include "rocksdb/experimental.h"

#include <algorithm>
#include <cmath>

using DynCompactionV3::TreeState;
using DynCompactionV3::DynAction;

void apply_to_tree(TreeState& state, const DynAction& action);
void get_win_acc_ios(int cur_total_runs, int max_offset, std::vector<double>& acc_ios, std::vector<double>& remain_rr, std::vector<double>& remain_p, int64_t buffer_size, double r, double u, double p);
DynAction get_best_action_with_forward(TreeState& cur_state, int tmp_look_forward, int64_t buffer_size, double r, double u, double p);

int adaptive_lookforward_simulate(const TreeState& cur_state, int64_t buffer_size, int& prev_max_lookforward, double r, double u, double p, bool fast_return) {
  int lf = 50;
  for (int i = 0; i < (int)cur_state.level_runs.size(); i++) {
    for (int j = 0; j < (int)cur_state.level_runs[i].size(); j++) {
      int b = std::floor(std::log2(std::max(2.0, cur_state.level_runs[i][j] * 1.0 / buffer_size)));
      lf += 2 * b;
    }
  }

  if (prev_max_lookforward != 0 && std::abs(lf - prev_max_lookforward) / prev_max_lookforward <= 0.1) {
    if (fast_return) {
      return 0;
    }
  }
  prev_max_lookforward = lf;
  
  int best_lookforward = 50;
  int best_total_runs = INT16_MAX;
  double best_cost = INT16_MAX;
  for (int i = 50; i < lf; i += 10) {
    auto state = cur_state;
    for (int j = 0; j < 10; j ++) {
      state.EnumerateActions();
      auto action = get_best_action_with_forward(state, i, buffer_size, r, u, p);
      apply_to_tree(state, action);
      for (int k = 0; k < action.estimate_finished_idx; k++) {
        state.level_runs[0].push_back(buffer_size);
        state.total_runs ++;
      }
    }
    if (state.total_runs < best_total_runs) {
      best_total_runs = state.total_runs;
      best_lookforward = i;
    }
  }
  return best_lookforward;
}

void apply_to_tree(TreeState& state, const DynAction& action) {
  if (action.start_level < 0) {
    return;
  }
  int start_level = action.start_level;
  int end_level = action.target_level;
  int64_t total_compacted_size = 0UL;
  for (int i = start_level; i <= end_level; i++) {
    if (action.removed_files[i].size() == 0) {
      continue;
    }
    for (int j = 0; j < (int)action.removed_files[i].size(); j++) {
      if (action.removed_files[i][j]) {
        total_compacted_size += state.level_runs[i][j];
        state.level_runs[i][j] = 0; // mark deleted
      }
    }
  }
  state.level_runs[end_level].push_back(total_compacted_size); // install the new run
  // calculate the total runs & remove the marked runs
  int total_runs = 0;
  int max_level_runs = 0;
  std::vector<std::vector<int64_t>> new_level_runs;
  for (int i = 0; i < (int)state.level_runs.size(); i++) {
    std::vector<int64_t> new_runs;
    for (int j = 0; j < (int)state.level_runs[i].size(); j++) {
      if (state.level_runs[i][j] > 0) {
        new_runs.push_back(state.level_runs[i][j]);
        total_runs ++;
      }
    }
    if (new_runs.size() > max_level_runs) {
      max_level_runs = new_runs.size();
    }
    // sort desc
    std::sort(new_runs.begin(), new_runs.end(), std::greater<int64_t>());
    new_level_runs.push_back(new_runs);
  }
  state.level_runs = new_level_runs;
  state.total_runs = total_runs;
  state.max_level_runs = max_level_runs;
}

void get_win_acc_ios(int cur_total_runs, int max_offset,
  std::vector<double>& acc_ios, std::vector<double>& remain_rr, std::vector<double>& remain_p, int64_t buffer_size, double r, double u, double p) {
  acc_ios.resize(max_offset, 0);
  remain_rr.resize(max_offset, 0);
  remain_p.resize(max_offset, 0);
  int cur_runs = cur_total_runs;
  double accio = 0;
  int64_t remain_rr_nums = r / u * 2048 * max_offset;
  int64_t remain_p_nums = p / u * 2048 * max_offset;
  for (int i = 0; i < max_offset; i++) {
    int64_t window_range_lookup_nums = r / u * 2048;
    int64_t window_point_lookup_nums = p / u * 2048;

    remain_rr_nums -= window_range_lookup_nums;
    remain_p_nums -= window_point_lookup_nums;

    accio += window_range_lookup_nums * cur_runs + buffer_size / 4096.0 + window_point_lookup_nums * (0.01 * cur_runs + 1);
    cur_runs ++;
    
    acc_ios[i] = accio;
    remain_rr[i] = remain_rr_nums;
    remain_p[i] = remain_p_nums;
  }
}

DynAction get_best_action_with_forward(TreeState& cur_state, int tmp_look_forward, int64_t buffer_size, double r, double u, double p) {
  DynAction best_action;
  std::vector<double> acc_ios, remain_rrs, remain_ps;
  get_win_acc_ios(cur_state.total_runs, tmp_look_forward, acc_ios, remain_rrs, remain_ps, buffer_size, r, u, p);
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