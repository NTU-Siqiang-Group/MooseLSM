#pragma once

#include <vector>
#include <cstdint>
#include <list>
#include <thread>
#include <memory>
#include <algorithm>
#include <unordered_map>

#include <eigen3/Eigen/Dense>
#include <string>
#include <iostream>


namespace DynCompactionV2 {
struct SearchNode {
  int32_t range_lookup_nums = 0;
  int32_t update_nums = 0;
  int32_t point_lookup_nums = 0;

  int64_t prefix_sum_range_lookups = 0;
  int64_t prefix_sum_point_lookups = 0;

  void Pop(const SearchNode& other) {
    range_lookup_nums -= other.range_lookup_nums;
    update_nums -= other.update_nums;
  }
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
  int start_level = -1;
  int target_level = -1;
  bool create_new = false;
  int min_idx_at_target_level = -1;
  
  // for evaluation
  int64_t compaction_size = 0;
  int reduced_runs = 0;
  int reward = 0;
};

struct CompactedActions {
  bool create_new = false;
  Eigen::MatrixXd compactions;
  Eigen::MatrixXd rewards;
};

struct DynActionV2 {
  int start_level = -1;
  int target_level = -1;
  int start_level_end_idx = -1;
  int end_level_end_idx = -1;
  bool create_new = false;
  bool is_major = false;
  double reward = 0;

  int estimate_finished_idx = -1;

  std::string ToString() const {
    return "Start level: " + std::to_string(start_level) + ", target level: " + std::to_string(target_level) 
    + ", start level end idx: " + std::to_string(start_level_end_idx) + ", create new: " + std::to_string(create_new)
    + ", is_major: " + std::to_string(is_major) + ", end_level_end_idx: " + std::to_string(end_level_end_idx)
    + ", reward: " + std::to_string(reward) + ", estimate finished idx: " + std::to_string(estimate_finished_idx);
  }
};

struct DynActionV3 {
  std::vector<std::vector<bool>> removed_files = decltype(removed_files)(20, std::vector<bool>(100, false));
  int start_level = -1;
  int target_level = -1;
  double reward = 0;
  int estimate_finished_idx = -1;

  std::string ToString() const {
    std::string ret;
    ret += "Start level: " + std::to_string(start_level) + ", target level: " 
      + std::to_string(target_level) + ", reward: " + std::to_string(reward) 
      + ", estimate finished idx: " + std::to_string(estimate_finished_idx) + "\n";
    return ret;
  }

  void setVal(int level, int start_idx, int end_idx, bool val=true) {
    for (int i = start_idx; i <= end_idx; i++) {
      removed_files[level][i] = val;
    }
  }
};

struct TreeState {
  std::vector<std::vector<int64_t>> level_runs;
  int total_runs = 0;

  // compacted actions
  CompactedActions create_new, // from i to i+1, create one new run
    with_next_run, // from i to i+1, compact to an existing run at i+1 level
    major_compaction, // from 0 to i, compact all runs from 0 to i-1 level to an existing run at i level
    major_compaction_new; // from 0 to i, compact all runs from 0 to i-1 level and create new run at i level
  
  int max_level_runs = 0;
  Eigen::MatrixXd create_new_reward_origin, with_next_run_reward_origin;
  Eigen::MatrixXd finished_idx_create_new, finished_idx_with_next, duration, prefix_range_lookups;
  // ------ end for compacted actions

  std::string ToString() const {
    std::string ret;
    for (int i = 0; i < (int)level_runs.size(); i++) {
      ret += "Level " + std::to_string(i) + ": " + std::to_string(level_runs[i].size()) + " runs, ";
      int64_t total_size = 0;
      for (int j = 0; j < (int)level_runs[i].size(); j++) {
        total_size += level_runs[i][j];
      }
      ret += "total size: " + std::to_string(total_size) + "\n";
    }
    // std::stringstream ss;
    // ss << "create_new_finished_idx: \n" << finished_idx_create_new << "\n";
    // ss << "with_next_run_finished_idx: \n" << finished_idx_with_next << "\n";
    // ss << "create_new_origin: \n" << create_new_reward_origin << "\n";
    // ss << "with_next_run_origin: \n" << with_next_run_reward_origin << "\n";
    // ss << "create_new_rewards: \n" << create_new.rewards << "\n";
    // ss << "with_next_run_rewards: \n" << with_next_run.rewards << "\n";
    // ss << "duration: \n" << duration << "\n";
    // ss << "prefix_range_lookups: \n" << prefix_range_lookups << "\n";
    // ret += ss.str();
    return ret;
  }

  void compaction_init() {
    create_new.compactions.resize(level_runs.size(), max_level_runs);
    create_new.rewards.resize(level_runs.size(), max_level_runs);
    create_new.rewards.setZero();
    create_new.compactions.setZero();
    
    with_next_run = create_new;
    with_next_run.create_new = false;

    major_compaction = create_new;
    major_compaction.create_new = false;
    major_compaction_new = create_new;
  }

  void InitCompactedActions() {
    compaction_init();
    int prev_total_size = 0;
    int prev_run_numbers = 0;
    std::vector<int64_t> level_sizes(level_runs.size(), 0);
    for (int i = 0; i < (int)level_runs.size(); i++) {
      int64_t cur_size = 0;
      if (level_runs[i].size() == 0) {
        continue;
      }
      for (int j = 0; j < (int)level_runs[i].size(); j++) {
        cur_size += level_runs[i][j];
        create_new.compactions(i, j) = j == 0 ? 0 : cur_size; // trivial move
        create_new.rewards(i, j) = j;
        if (j == (int)level_runs[i].size() - 1 && i != (int)level_runs.size() - 1) {
          if (level_runs[i + 1].size() > 0) {
            with_next_run.compactions(i, j) = cur_size + level_runs[i + 1].back();
            with_next_run.rewards(i, j) = j + 1;
          }
        }
        if (i != 0) {
          major_compaction.compactions(i, j) = prev_total_size + cur_size;
          major_compaction.rewards(i, j) = prev_run_numbers + j;
        }
      }
      level_sizes[i] = cur_size;
      prev_total_size += cur_size;
      prev_run_numbers += level_runs[i].size();
    }
    if (max_level_runs > 0) {
      int prev_level_run = level_runs[0].size();
      for (int i = 1; i < (int)level_runs.size(); i++) {
        // create new major
        major_compaction_new.compactions(i, 0) = level_sizes[i - 1] + major_compaction_new.compactions(i - 1, 0);
        major_compaction_new.rewards(i, 0) = prev_level_run - 1;
        prev_level_run += level_runs[i].size();
      }
    int rw_ratio = 2;
      create_new.compactions *= rw_ratio / 4096.0;
      major_compaction.compactions *= rw_ratio / 4096.0;
      major_compaction_new.compactions *= rw_ratio / 4096.0;
      with_next_run.compactions *= rw_ratio / 4096.0;
    }
  }

  std::vector<DynAction> EnumerateActions() const {
    std::vector<DynAction> actions;
    actions.push_back(DynAction()); // do nothing
    for (int i = 0; i < (int)level_runs.size(); i++) {
      if (level_runs[i].size() == 0) {
        continue;
      }
      DynAction action;
      action.start_level = i;
      action.reduced_runs = level_runs[i].size() - 1;
      action.target_level = i + 1 < (int)level_runs.size() ? i + 1 : i;
      action.create_new = true;
      // all runs at i-th level
      for (int j = 0; j < (int)level_runs[i].size(); j++) {
        action.compaction_size += level_runs[i][j];
      }
      actions.push_back(action);
      
      if (action.target_level == i) {
        // if we are compacting the file at the bottommost level
        // we can only create a new file
        continue;
      }
      // find the smallest run at (i+1)-th level
      if (action.target_level < (int)level_runs.size()
        && (int)level_runs[action.target_level].size() > 0) {
        int64_t min_size = INT64_MAX;
        int min_idx = -1;
        for (int j = 0; j < (int)level_runs[action.target_level].size(); j++) {
          if (level_runs[action.target_level][j] < min_size) {
            min_size = level_runs[action.target_level][j];
            min_idx = j;
          }
        }
        action.compaction_size += min_size;
        action.min_idx_at_target_level = min_idx;
        action.create_new = false;
        action.reduced_runs ++;
        actions.push_back(action);
      }
    }
    return actions;
  }
};

struct DynamicCompactionerV2 {
  Sequence workload;
  int64_t buffer_size;

  std::atomic<int> lookforward{100};

  void gen_window_duration(const TreeState& tree_state, int start_win_idx, int max_forward_offset,
      Eigen::MatrixXd& window_duration, Eigen::MatrixXd& prefix_range_lookups, Eigen::MatrixXd& prefix_point_lookups) {
    Eigen::MatrixXd row_vec = Eigen::MatrixXd::Zero(1, max_forward_offset);
    for (int i = 0; i < max_forward_offset; i++) {
      row_vec(0, i) = tree_state.total_runs + i;
    }
    window_duration = Eigen::MatrixXd::Zero(1, max_forward_offset);
    prefix_range_lookups = Eigen::MatrixXd::Zero(1, max_forward_offset);
    prefix_point_lookups = Eigen::MatrixXd::Zero(1, max_forward_offset);
    for (int i = start_win_idx; i < start_win_idx + max_forward_offset; i++) {
      int64_t range_lookup_nums = workload.At(i).range_lookup_nums;
      int64_t point_lookup_nums = workload.At(i).point_lookup_nums;
      window_duration(0, i - start_win_idx) = range_lookup_nums * row_vec(0, i - start_win_idx) + (1 + 0.01 * row_vec(0, i - start_win_idx)) * point_lookup_nums + buffer_size / 4096;
      if (i != start_win_idx) {
        window_duration(0, i - start_win_idx) += window_duration(0, i - start_win_idx - 1);
      }
      prefix_range_lookups(0, i - start_win_idx) = workload.At(start_win_idx + max_forward_offset - 1).prefix_sum_range_lookups - workload.At(i).prefix_sum_range_lookups;
      prefix_point_lookups(0, i - start_win_idx) = workload.At(start_win_idx + max_forward_offset - 1).prefix_sum_point_lookups - workload.At(i).prefix_sum_point_lookups;
    }
  }

  Eigen::MatrixXd find_min_idx(const Eigen::MatrixXd& mat, const Eigen::MatrixXd& vec) {
    // vec is [1, n]
    Eigen::MatrixXd count = Eigen::MatrixXd::Zero(mat.rows(), mat.cols());
    for (int i = 0; i < vec.cols(); i++) {
      Eigen::MatrixXd mask = Eigen::MatrixXd::Constant(mat.rows(), mat.cols(), vec(0, i));
      count += (Eigen::MatrixXd)((mask.array() < mat.array()).cast<double>().reshaped(mat.rows(), mat.cols()));
    }
    return count;
  }
  
  bool is_trivial_move(const TreeState& state, const DynAction& action) const {
    if (action.start_level >= 0 && state.level_runs[action.start_level].size() == 1) {
      return true;
    }
    return false;
  }
  
  TreeState evaluate_reward_and_next_state(const TreeState& current_state, DynAction& action, int start_win_idx, int& end_win_idx) {
    if (action.start_level < 0) {
      // do nothing
      auto state = current_state;
      end_win_idx = start_win_idx + 1;
      action.reward = 0;
      state.level_runs[0].push_back(buffer_size);
      return state;
    }
    if (is_trivial_move(current_state, action)) {
      // no need to do anything
      auto state = current_state;
      end_win_idx = start_win_idx + 1;
      action.reward = 0;
      state.level_runs[0].push_back(buffer_size);
      return state;
    }
    auto state = current_state;
    int consumed_ios = action.compaction_size / 4096 * 1.5;
    int cumulative_ios = 0;
    int win_idx = start_win_idx;
    int added_runs = 0;
    
    while (cumulative_ios < consumed_ios) {
      auto window = workload.At(win_idx);
      cumulative_ios += get_range_lookup_io(state, window.range_lookup_nums) + buffer_size / 4096;
      state.total_runs ++;
      state.level_runs[0].push_back(buffer_size);
      added_runs ++;
      win_idx ++;
    }
    // remove the whole level of run at start_level
    state.total_runs -= state.level_runs[action.start_level].size();
    state.level_runs[action.start_level].clear();
    if (action.create_new) {
      state.level_runs[action.target_level].push_back(action.compaction_size);
    } else {
      // find the smallest run at target_level
      int min_idx = action.min_idx_at_target_level;
      state.total_runs --;
      state.level_runs[action.target_level].erase(state.level_runs[action.target_level].begin() + min_idx);
      state.level_runs[action.target_level].push_back(action.compaction_size);
    }
    end_win_idx = win_idx;
    action.reward = action.reduced_runs - added_runs;

    return state;
  }

  int get_range_lookup_io(const TreeState& state, int range_looup_nums) const {
    return range_looup_nums * state.total_runs;
  }

  DynAction GetBestAction(const TreeState& current_state, int start_win_idx, int depth = 0) {
    if (depth >= 1) {
      return DynAction();
    }
    auto actions = current_state.EnumerateActions();
    int best_reward = INT32_MIN;
    DynAction best_action;

    int max_level = current_state.level_runs.size() - 1;
    for (auto& action : actions) {
      int end_win_idx = start_win_idx;
      auto new_state = evaluate_reward_and_next_state(current_state, action, start_win_idx, end_win_idx);
      auto next_action = GetBestAction(new_state, end_win_idx, depth + 1);
      action.reward += next_action.reward;
      if (action.reward > best_reward
        || (action.reward == best_reward && best_action.start_level < 0 && action.target_level < max_level)
      ) {
        best_reward = action.reward;
        best_action = action;
      }
    }
    return best_action;
  }

  void get_estimate_reward(Eigen::MatrixXd& rewards, const Eigen::MatrixXd& finished_idx,
      const Eigen::MatrixXd& prefix_range_lookups, const Eigen::MatrixXd& prefix_point_lookups) {
    for (int i = 0; i < rewards.rows(); i++) {
      for (int j = 0; j < rewards.cols(); j++) {
        double r = rewards(i, j);
        if (r <= 0) {
          continue;
        }
        int idx = finished_idx(i, j);
        if (idx >= prefix_range_lookups.cols()) {
          r = 0;
        } else {
          r *= prefix_range_lookups(0, idx) + prefix_point_lookups(0, idx) * 0.01; // Assumption: point lookup is 100 times faster
          int prev_range_lookup_num = prefix_range_lookups(0, 0) - prefix_range_lookups(0, idx);
          double cost = (double)idx * prev_range_lookup_num / 2;
          int prev_point_lookup_num = prefix_point_lookups(0, 0) - prefix_point_lookups(0, idx);
          cost += prev_point_lookup_num * 0.01 / 2;
          r -= cost;
        }
        rewards(i, j) = r;
      }
    }
  }

  DynActionV2 GetBestActionWithCompactedActions(TreeState& current_state, int start_win_idx, int depth = 0) {
    // look forward 100 windows
    if (current_state.max_level_runs == 0 || workload.windows.size() == 0) {
      return DynActionV2();
    }
    current_state.create_new_reward_origin = current_state.create_new.rewards;
    current_state.with_next_run_reward_origin = current_state.with_next_run.rewards;
    DynActionV2 best_action, tmp;
    Eigen::MatrixXd duration, prefix_range_lookups, prefix_point_lookups;
    gen_window_duration(current_state, start_win_idx, lookforward, duration, prefix_range_lookups, prefix_point_lookups);
    current_state.duration = duration;
    current_state.prefix_range_lookups = prefix_range_lookups;

    auto finished_idx = find_min_idx(current_state.create_new.compactions, duration);
    current_state.finished_idx_create_new = finished_idx;
    get_estimate_reward(current_state.create_new.rewards, finished_idx, prefix_range_lookups, prefix_point_lookups);

    // find the minimum element's idx at reward matrix
    int row = 0, col = 0;
    tmp.reward = current_state.create_new.rewards.maxCoeff(&row, &col);
    tmp.start_level = row;
    tmp.target_level = row + 1 < (int)current_state.level_runs.size() ? row + 1 : row;
    tmp.start_level_end_idx = col;
    if (tmp.start_level_end_idx != (int)current_state.level_runs[row].size() - 1) {
      // not compacting the whole level
      // we cannot put it to the next level
      tmp.target_level = tmp.start_level;
    }
    tmp.create_new = true;
    tmp.estimate_finished_idx = finished_idx(row, col);
    if (tmp.reward > best_action.reward) {
      best_action = tmp;
    }

    tmp = DynActionV2();
    finished_idx = find_min_idx(current_state.with_next_run.compactions, duration);
    get_estimate_reward(current_state.with_next_run.rewards, finished_idx, prefix_range_lookups, prefix_point_lookups);
    current_state.finished_idx_with_next = finished_idx;
    tmp.reward = current_state.with_next_run.rewards.maxCoeff(&row, &col);
    tmp.start_level = row;
    tmp.target_level = row + 1 < (int)current_state.level_runs.size() ? row + 1 : row;
    tmp.start_level_end_idx = col;
    tmp.create_new = false;
    tmp.estimate_finished_idx = finished_idx(row, col);
    if (tmp.reward > best_action.reward) {
      best_action = tmp;
    }

    tmp = DynActionV2();
    finished_idx = find_min_idx(current_state.major_compaction.compactions, duration);
    get_estimate_reward(current_state.major_compaction.rewards, finished_idx, prefix_range_lookups, prefix_point_lookups);
    tmp.reward = current_state.major_compaction.rewards.maxCoeff(&row, &col);
    tmp.start_level = 0;
    tmp.target_level = row;
    tmp.end_level_end_idx = col;
    tmp.create_new = false;
    tmp.is_major = true;
    tmp.estimate_finished_idx = finished_idx(row, col);
    if (tmp.reward > best_action.reward) {
      best_action = tmp;
    }

    tmp = DynActionV2();
    finished_idx = find_min_idx(current_state.major_compaction_new.compactions, duration);
    get_estimate_reward(current_state.major_compaction_new.rewards, finished_idx, prefix_range_lookups, prefix_point_lookups);
    tmp.reward = current_state.major_compaction_new.rewards.maxCoeff(&row, &col);
    tmp.start_level = 0;
    tmp.target_level = row;
    tmp.create_new = true;
    tmp.is_major = true;
    tmp.estimate_finished_idx = finished_idx(row, col);
    if (tmp.reward > best_action.reward) {
      best_action = tmp;
    }

    // return the one with maximum reward
    return best_action;
  }

  DynActionV3 GetBestActionV3(TreeState& current_state, int start_win_idx, int depth=0) {
    if (current_state.max_level_runs == 0 || workload.windows.size() == 0) {
      return DynActionV3();
    }
    current_state.create_new_reward_origin = current_state.create_new.rewards;
    current_state.with_next_run_reward_origin = current_state.with_next_run.rewards;
    DynActionV3 best_action, tmp;
    Eigen::MatrixXd duration, prefix_range_lookups, prefix_point_lookups;
    gen_window_duration(current_state, start_win_idx, lookforward, duration, prefix_range_lookups, prefix_point_lookups);
    current_state.duration = duration;
    current_state.prefix_range_lookups = prefix_range_lookups;

    int row = 0, col = 0;
    auto finished_idx = find_min_idx(current_state.create_new.compactions, duration);
    current_state.finished_idx_create_new = finished_idx;
    get_estimate_reward(current_state.create_new.rewards, finished_idx, prefix_range_lookups, prefix_point_lookups);
    tmp.reward = current_state.create_new.rewards.maxCoeff(&row, &col);
    // find the minimum element's idx at reward matrix
    if (tmp.reward > best_action.reward) {
      tmp.start_level = row;
      tmp.target_level = row + 1 < (int)current_state.level_runs.size() ? row + 1 : row;
      tmp.removed_files[row] = std::vector<bool>(current_state.level_runs[row].size(), false);
      tmp.setVal(row, 0, col);
      if (col != (int)current_state.level_runs[row].size() - 1) {
        // not compacting the whole level
        // we cannot put it to the next level
        tmp.target_level = tmp.start_level;
      }
      tmp.estimate_finished_idx = finished_idx(row, col);
    
      best_action = tmp;
    }
    
    tmp = DynActionV3();
    finished_idx = find_min_idx(current_state.with_next_run.compactions, duration);
    get_estimate_reward(current_state.with_next_run.rewards, finished_idx, prefix_range_lookups, prefix_point_lookups);
    current_state.finished_idx_with_next = finished_idx;
    tmp.reward = current_state.with_next_run.rewards.maxCoeff(&row, &col);
    if (tmp.reward > best_action.reward) {
      tmp.start_level = row;
      tmp.target_level = row + 1 < (int)current_state.level_runs.size() ? row + 1 : row;
      tmp.removed_files[row] = std::vector<bool>(current_state.level_runs[row].size(), false);
      // tmp.start_level_end_idx = col;
      // tmp.create_new = false;
      tmp.setVal(row, 0, col);
      auto back_idx = current_state.level_runs[tmp.target_level].size() - 1;
      tmp.removed_files[tmp.target_level] = std::vector<bool>(current_state.level_runs[tmp.target_level].size(), false);
      tmp.setVal(tmp.target_level, back_idx, back_idx); // compact the last one
      tmp.estimate_finished_idx = finished_idx(row, col);
    
      best_action = tmp;
    }

    tmp = DynActionV3();
    finished_idx = find_min_idx(current_state.major_compaction.compactions, duration);
    get_estimate_reward(current_state.major_compaction.rewards, finished_idx, prefix_range_lookups, prefix_point_lookups);
    tmp.reward = current_state.major_compaction.rewards.maxCoeff(&row, &col);
    if (tmp.reward > best_action.reward) {
      tmp.start_level = 0;
      tmp.target_level = row;
      for (int i = 0; i <= tmp.target_level; i++) {
        tmp.removed_files[i] = std::vector<bool>(current_state.level_runs[i].size(), false);
        if (i == tmp.target_level) {
          // tmp.level_remove_end_idxs[i] = col;
          tmp.setVal(i, 0, col);
        } else {
          tmp.setVal(i, 0, current_state.level_runs[i].size() - 1); // remove the whole level
        }
      }
      tmp.estimate_finished_idx = finished_idx(row, col);
      best_action = tmp;
    }

    tmp = DynActionV3();
    finished_idx = find_min_idx(current_state.major_compaction_new.compactions, duration);
    get_estimate_reward(current_state.major_compaction_new.rewards, finished_idx, prefix_range_lookups, prefix_point_lookups);
    tmp.reward = current_state.major_compaction_new.rewards.maxCoeff(&row, &col);
    if (tmp.reward > best_action.reward) {
      tmp.start_level = 0;
      tmp.target_level = row;
      for (int i = 0; i < tmp.target_level; i++) {
        tmp.setVal(i, 0, current_state.level_runs[i].size() - 1); // remove the whole level
      }
      tmp.estimate_finished_idx = finished_idx(row, col);
    
      best_action = tmp;
    }

    // return the one with maximum reward
    return best_action;
  }

  DynamicCompactionerV2(int64_t buffsize, int lf=100): buffer_size(buffsize), lookforward(lf)  {}
};

} // namespace DynCompactionV2