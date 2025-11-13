#include "rocksdb/options.h"
#include "rocksdb/experimental.h"

#include <algorithm>
#include <cmath>
#include <omp.h>

using DynCompactionV4::TreeState;
using DynCompactionV4::DynAction;
using DynCompactionV4::DynamicCompactionerV4;

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



DynAction get_best_action_with_forward(TreeState& cur_state, int M, int c,
    int64_t buffer_size, int r, int u, int p, double wait_io, double parallel_factor) {
  DynAction best_action;
  std::vector<double> acc_ios;
  DynamicCompactionerV4::get_win_acc_ios(cur_state.total_runs, 500, acc_ios, buffer_size, c, r, u, p, wait_io, parallel_factor);
  // get estimate finished idx for action
  for (auto& action : cur_state.actions) {
    DynamicCompactionerV4::get_reward_for_action(action, cur_state.total_runs, acc_ios, c, M, r, u, p, buffer_size, wait_io, parallel_factor);
    if (action.reward > best_action.reward) {
      best_action = action;
    }
  }
  return best_action;
}

double get_cost_for_mc(const TreeState& latest_state, int M, int c, int64_t buffer_size,
  int r, int u, int p, double wait_io, int mc_search_len, int remaining_window_cnt, double parallel_factor) {
  double cost = 0;
  int64_t ops = 0;
  auto tmp_state = latest_state;
  double factor = parallel_factor;
  if (factor > 1) {
    factor /= 2;
  }
  int iter_cnt = 0;
  for (int i = 0; i < mc_search_len; i++) {
    iter_cnt ++;
    tmp_state.actions.clear();
    // auto start = std::chrono::high_resolution_clock::now();
    tmp_state.EnumerateActions();
    // auto end = std::chrono::high_resolution_clock::now();
    // std::cout << "enum time: " << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() << "us" << std::endl;
    auto action = get_best_action_with_forward(tmp_state, M, c, buffer_size, r, u, p, wait_io, parallel_factor);
    if (action.start_level < 0) {
      action.estimate_finished_idx = 0;
    }
    // std::cout << action.ToString() << std::endl;
    int64_t total_runs = tmp_state.total_runs;
    double remaining_compaction_size = action.compaction_size;

    int elasped_window_cnt = action.estimate_finished_idx + 1;

    if (remaining_window_cnt < remaining_window_cnt) {
      elasped_window_cnt = remaining_window_cnt;
    }

    remaining_window_cnt -= elasped_window_cnt;

    for (int j = 0; j < elasped_window_cnt; j++) {
      double tmp_cost = total_runs * r + (total_runs * 0.01 + 1) * p + buffer_size / 4096.0;
      // double tmp_cost = total_runs * r + (total_runs * 1.01) * p + buffer_size / 4096.0;
      cost += tmp_cost;
      remaining_compaction_size -= tmp_cost / factor;
      if (total_runs >= c && total_runs < c * 4) {
        cost += 1.0 * u * DynamicCompactionerV4::kStallCost;
        remaining_compaction_size -= 1.0 * u * DynamicCompactionerV4::kStallCost;
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
  // std::cout << "M: " << M << ", c: " << c << ", cost: " << cost << ", ops: " << ops << std::endl;
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
  for (int i = 4; i <= upper; i += 1) {
    c_candidates.push_back(i);
  }
  c_candidates.push_back(1000000);
  for (int M = 2; M < 50; M += 1) {
    for (int c : c_candidates) {
      results.push_back(std::make_tuple(M, c, 0));   
    }
  }
  int counter = 0;
  omp_set_num_threads(16);
  #pragma omp parallel for
  for (int i = 0; i < (int)results.size(); i++) {
    auto [M, c, _] = results[i];
    double cost = get_cost_for_mc(latest_state, M, c, buffer_size, r, u, p, wait_io, mc_search_len, remaining_window_cnt, parallel_factor);
    // std::lock_guard<std::mutex> guard(mtx);
    mtx.lock();
    std::get<2>(results[i]) = cost;
    counter ++;
    // std::cout << "M: " << M << ", c: " << c << ", cost: " << cost << std::endl;
    mtx.unlock();
  }
  // std::cout << "len of results: " << counter << ", size: " << results.size() << std::endl;
  // find the best M and c
  for (auto& [M, c, cost] : results) {
    if (cost < best_cost) {
      best_cost = cost;
      best_M = M;
      best_c = c;
    }
  }
  // std::cout << "best cost: " << best_cost << ", best M: " << best_M << ", best c: " << best_c << std::endl;
  return std::make_pair(best_M, best_c);
}

int get_reduced_runs(const DynAction& action) {
  int reduced_runs = 0;
  for (int i = 0; i < (int)action.removed_files.size(); i++) {
    for (int j = 0; j < (int)action.removed_files[i].size(); j++) {
      if (action.removed_files[i][j]) {
        reduced_runs ++;
      }
    }
  }
  reduced_runs -= 1;
  return reduced_runs;
}

int main() {
  std::vector<double> rrs = {0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9};
  std::vector<double> wrs = {0.9, 0.8, 0.7, 0.6, 0.5, 0.4, 0.3, 0.2, 0.1};
  std::vector<int> run_nums;
  for (int i = 1; i <= 40; i++) {
    run_nums.push_back(i);
  }

  TreeState state;
  int64_t buffer_size = 2L * (1<<20);
  state.level_runs.resize(4);
  state.level_runs[3].push_back(40UL * (1<<30));
  state.level_runs[2].push_back(20UL * (1<<30));
  state.level_runs[2].push_back(10UL * (1<<30));
  int64_t run_size = buffer_size * 20;
  // for (int i = 0; i < (int)rrs.size(); i++) {
  //   for (int j = 0; j < run_nums.size(); j++) {
      int u = 2048, p = 0;
      int r = 2048;
      auto tmp_state = state;
      // for (int k = 0; k < run_nums[j]; k++) {
      //   tmp_state.level_runs[0].push_back(run_size);
      // }
      tmp_state.total_runs = 3;
      tmp_state.max_level_runs = 2;
      auto [M, c] = FindBestMC(tmp_state, buffer_size, r, u, p, 0, 400, 10000, 1);
      double estimate_cost = get_cost_for_mc(tmp_state, M, c, buffer_size, r, u ,p, 0, 10, 10000, 1);
      // std::cout << "r: " << r << ", u: " << u << ", p: " << p
      //   << ", M: " << M << ", c: " << c
      //   << ", rratio: " << r << ", wratio: " << u
      //   << ", run_nums: " << run_nums[j] 
      //   << std::endl;
      // std::cout << "--------------------------------" << std::endl;
  //   }
  // }
  return 0;
}