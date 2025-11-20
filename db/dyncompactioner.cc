#include "rocksdb/dyncompactionv4.h"
#include "rocksdb/dynamic_lookforward.h"

namespace DynCompaction {
double DynamicCompactioner::kStallCost = 4;
double DynamicCompactioner::kStoppedCost = 1e10;
void DynamicCompactioner::get_win_acc_ios(int cur_total_runs,
    int max_offset, std::vector<double>& acc_ios, int64_t buffer_size,
    int write_stall, int r, int u, int p, double wait_io, double parallel_factor) {
  acc_ios.resize(max_offset, 0);
  double acc_io = 0;
  for (int i = 0; i < max_offset; i++) {
    acc_io += buffer_size * 1.0 / 4096 + r * cur_total_runs + p * (1 + cur_total_runs * 0.01);
    if (cur_total_runs >= write_stall) {
      acc_io += u * kStallCost;
    }
    if (cur_total_runs >= write_stall * 4) {
      acc_io += u * kStoppedCost;
    }
    acc_io += (r + u + p) * wait_io;
    double factor = parallel_factor;
    if (parallel_factor > 1) {
      factor /= 2;
    }
    acc_ios[i] = acc_io / factor;
    cur_total_runs += 1;
  }
}

void DynamicCompactioner::get_reward_for_action(DynAction& action, int total_runs, const std::vector<double>& acc_ios, 
    int c, int M, int r, int u, int p, int buffer_size, double wait_io, double parallel_factor) {
  int idx = 0;
  auto it = std::lower_bound(acc_ios.begin(), acc_ios.end(), action.compaction_size);
  idx = it - acc_ios.begin();
  if (it == acc_ios.end() || idx >= (int)acc_ios.size() - 1) {
    action.reward = 0;
    return;
  }

  double factor = parallel_factor;
  if (factor > 1) {
    factor /= 2;
  }

  action.estimate_finished_idx = idx;
  action.reward *= (r + p * 0.01) * M;
  if (idx > 0) {
    double inc_rr = idx * 1.0 * r / 2;
    double inc_p = idx * 0.01 * p / 2;
    double inc_w = 0;
    if (idx + total_runs >= c) {
      inc_w = std::max(0, idx + total_runs - c) * DynamicCompactioner::kStallCost * u;
    }
    double remaining_comp = action.compaction_size - inc_rr - inc_p - inc_w;
    if (idx + total_runs >= 4 * c) {
      inc_w += std::max(0.0, remaining_comp) * factor;
    }
    double cost = inc_rr + inc_p + inc_w;
    // std::cout << std::fixed << c << "," << cost << "," << action.compaction_size << std::endl;
    action.reward -= cost;
  }
}
} // namespace DynCompactionV4

namespace DynamicLookForward {
using DynCompaction::TreeState;
using DynCompaction::DynAction;
using DynCompaction::DynamicCompactioner;

static void apply_to_tree(TreeState& state, const DynAction& action) {
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
    if ((int)new_runs.size() > max_level_runs) {
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

static DynAction get_best_action_with_forward(TreeState& cur_state, int M, int c,
    int64_t buffer_size, int r, int u, int p, double wait_io, double parallel_factor) {
  DynAction best_action;
  std::vector<double> acc_ios;
  DynamicCompactioner::get_win_acc_ios(cur_state.total_runs, 500, acc_ios, buffer_size, c, r, u, p, wait_io, parallel_factor);
  // get estimate finished idx for action
  for (auto& action : cur_state.actions) {
    DynamicCompactioner::get_reward_for_action(action, cur_state.total_runs, acc_ios, c, M, r, u, p, buffer_size, wait_io, parallel_factor);
    if (action.reward > best_action.reward) {
      best_action = action;
    }
  }
  return best_action;
}

static double get_cost_for_mc(const TreeState& latest_state, int M, int c, int64_t buffer_size,
    int r, int u, int p, double wait_io, int mc_search_len, int remaining_window_cnt, double parallel_factor) {
  double cost = 0;
  int64_t ops = 0;
  // std::cout << r << ", " << u << ", " << p << ", " << remaining_window_cnt << std::endl;
  auto tmp_state = latest_state;
  double factor = parallel_factor;
  if (factor > 1) {
    factor /= 2;
  }
  for (int i = 0; i < mc_search_len; i++) {
    tmp_state.actions.clear();
    tmp_state.EnumerateActions();
    auto action = get_best_action_with_forward(tmp_state, M, c, buffer_size, r, u, p, wait_io, parallel_factor);
    if (action.start_level < 0) {
      action.estimate_finished_idx = 0;
    }
    // std::cout << action.ToString() << std::endl;
    int64_t total_runs = tmp_state.total_runs;
    double remaining_compaction_size = action.compaction_size;

    int elasped_window_cnt = action.estimate_finished_idx + 1;

    if (elasped_window_cnt < remaining_window_cnt) {
      elasped_window_cnt = remaining_window_cnt;
    }

    remaining_window_cnt -= elasped_window_cnt;

    for (int j = 0; j < elasped_window_cnt; j++) {
      double tmp_cost = total_runs * r + (total_runs * 0.01 + 1) * p + buffer_size / 4096.0;
      // double tmp_cost = total_runs * r + (total_runs * 1.01) * p + buffer_size / 4096.0;
      cost += tmp_cost;
      remaining_compaction_size -= tmp_cost / factor;
      if (total_runs >= c && total_runs < c * 4) {
        cost += 1.0 * u * DynamicCompactioner::kStallCost;
        remaining_compaction_size -= 1.0 * u * DynamicCompactioner::kStallCost;
      }
      if (total_runs >= c * 4) {
        // stop the write until the compaction is done
        // std::cout << "Trigger write stop: " << j << "/" << action.estimate_finished_idx << std::endl;
        cost += std::max(0.0, remaining_compaction_size) * factor;
        remaining_compaction_size = 0;
      }
      total_runs ++;
      ops += r + p + u;
    }
    apply_to_tree(tmp_state, action);
    for (int k = 0; k < elasped_window_cnt; k++) {
      tmp_state.level_runs[0].push_back(buffer_size);
      tmp_state.total_runs ++;
    }
    if ((int)tmp_state.level_runs[0].size() > tmp_state.max_level_runs) {
      tmp_state.max_level_runs = tmp_state.level_runs[0].size();
    }
    if (remaining_window_cnt <= 0) {
      break;
    }
  }
  double avg_cost = cost / ops;
  if (avg_cost <= 0) {
    std::cout << "Overflow!!!!!: " << avg_cost 
    << ", M: " << M << ", c: " << c
    << ", r: " << r << ", u: " << u
    << std::endl;
    avg_cost = 1e9;
  }
  return avg_cost;
}

std::pair<int, int> FindBestMC(const TreeState& latest_state, int64_t buffer_size, 
    int r, int u, int p, double wait_io, int mc_search_len, int remaining_window_cnt, double parallel_factor) {
  double best_cost = 1e9;
  int best_M = 0, best_c = 0;
  std::vector<int> c_candidates = {4};
  int upper = std::max(4 * 2, latest_state.total_runs * 2);
  std::mutex mtx;
  std::vector<std::tuple<int, int, double>> results;
  for (int i = 4; i <= upper; i += 4) {
    c_candidates.push_back(i);
  }
  c_candidates.push_back(1000000);
  for (int M = 5; M < 100; M += 5) {
    for (int c : c_candidates) {
      results.push_back(std::make_tuple(M, c, 0));   
    }
  }
  omp_set_num_threads(16);
  #pragma omp parallel for
  for (int i = 0; i < (int)results.size(); i++) {
    auto [M, c, _] = results[i];
    double cost = get_cost_for_mc(latest_state, M, c, buffer_size, r, u, p, wait_io, mc_search_len, remaining_window_cnt, parallel_factor);
    std::lock_guard<std::mutex> guard(mtx);
    std::get<2>(results[i]) = cost;
    // std::cout << "M: " << M << ", c: " << c << ", cost: " << cost << std::endl;
  }
  // find the best M and c
  for (auto& [M, c, cost] : results) {
    if (cost < best_cost) {
      best_cost = cost;
      best_M = M;
      best_c = c;
    }
  }
  std::cout << "best cost: " << best_cost << ", best M: " << best_M << ", best c: " << best_c << std::endl;
  return std::make_pair(best_M, best_c);
}
}