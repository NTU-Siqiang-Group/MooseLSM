#pragma once

#include <vector>
#include <cstdint>
#include <list>
#include <thread>
#include <memory>
#include <algorithm>
#include <unordered_map>

namespace DynCompaction {
struct SearchNode {
  uint32_t range_lookup_nums = 0;
  uint32_t update_nums = 0;
  uint32_t op_gap_nano = 0; // in nano secs
  uint64_t request_duration_micro = 0; // in micro secs

  void PopFront(const SearchNode& other) {
    uint64_t total_gaps = op_gap_nano * (range_lookup_nums + update_nums);
    uint64_t other_total_gaps = other.op_gap_nano * (other.range_lookup_nums + other.update_nums);
    range_lookup_nums -= other.range_lookup_nums;
    update_nums -= other.update_nums;
    request_duration_micro -= other.request_duration_micro;
    total_gaps -= other_total_gaps;
    if (range_lookup_nums + update_nums == 0) {
      op_gap_nano = 0;
    } else {
      op_gap_nano = total_gaps / (range_lookup_nums + update_nums);
    }
  }
};

namespace {
double estimate_window_micro(const SearchNode& node, const double stall_speed,
                            const double est_update_time, const double est_range_lookup_time_perrun,
                            const int l0_slowdown, bool is_stall, int key_size, int val_size) {
  const int thread_num = 32;
  int bubbles = thread_num * 2;
  double range_prop = (double)node.range_lookup_nums / (node.range_lookup_nums + node.update_nums);
  double update_prop = 1 - range_prop;
  if (!is_stall) {
    return (double)node.request_duration_micro + bubbles * (range_prop * est_range_lookup_time_perrun + update_prop * est_update_time);
  }
  double stall_time_per_update = (double)(key_size + val_size) / stall_speed * 1e6; // us
  return (double)node.request_duration_micro + bubbles * (range_prop * est_range_lookup_time_perrun + update_prop * (est_update_time + stall_time_per_update));
}
}

struct SearchTree {
  std::vector<SearchNode> windows;
  int cur_idx = 0;
  SearchTree() {}
  SearchTree(const std::vector<SearchNode>& win) {
    std::copy(win.begin(), win.end(), std::back_inserter(windows));
  }

  SearchNode at(int idx) const {
    return windows[idx];
  }

  void Tick() {
    cur_idx ++;
  }

  SearchNode GetMergedSketch(int start) {
    SearchNode merged_sketch;
    uint64_t total_gaps = 0;
    for (int i = start; i < (int)windows.size(); i++) {
      merged_sketch.range_lookup_nums += windows[i].range_lookup_nums;
      merged_sketch.update_nums += windows[i].update_nums;
      merged_sketch.request_duration_micro += windows[i].request_duration_micro;
      total_gaps += windows[i].op_gap_nano * (windows[i].range_lookup_nums + windows[i].update_nums);
    }
    if (merged_sketch.range_lookup_nums + merged_sketch.update_nums == 0) {
      return merged_sketch;
    }
    merged_sketch.op_gap_nano = total_gaps / (merged_sketch.range_lookup_nums + merged_sketch.update_nums);
    return merged_sketch;
  }
};

struct DynAction {
  // {level, idx}
  std::vector<std::pair<int, int>> merged_runs = {};
  double reward = 0;
  int target_level = 0;
  uint64_t compaction_size = 0;
};

struct TreeState {
  std::shared_ptr<TreeState> best_nxt_state;
  DynAction best_action;
  std::vector<std::vector<uint64_t>> level_runs;
  SearchNode merged_sketch;
  int start_win_idx = 0;

  std::vector<DynAction> EnumerateValidAction() const {
    std::vector<DynAction> actions;
    DynAction do_nothing;
    do_nothing.reward = 0;
    do_nothing.target_level = 0;
    actions.push_back(do_nothing);
    for (int i = 0; i < (int)level_runs.size(); i++) {
      if (level_runs[i].size() == 0) {
        continue;
      }
      DynAction action;
      action.target_level = i + 1 < (int)level_runs.size() ? i + 1 : i;
      // all runs at i-th level
      for (int j = 0; j < (int)level_runs[i].size(); j++) {
        action.merged_runs.push_back({i, j});
        action.compaction_size += level_runs[i][j];
      }
      if (action.target_level == i && action.merged_runs.size() <= 1) {
        continue;
      }
      actions.push_back(action);
      // find the smallest run at (i+1)-th level
      if (i + 1 < (int)level_runs.size() && (int)level_runs[i + 1].size() > 0) {
        uint64_t min_size = UINT64_MAX;
        int min_idx = -1;
        for (int j = 0; j < (int)level_runs[i + 1].size(); j++) {
          if (level_runs[i + 1][j] < min_size) {
            min_size = level_runs[i + 1][j];
            min_idx = j;
          }
        }
        action.merged_runs.push_back({i + 1, min_idx});
        action.compaction_size += min_size;
        actions.push_back(action);
      }
    }
    return actions;
  }
  bool Eq(const TreeState& other) const {
    if (level_runs.size() != other.level_runs.size()) {
      return false;
    }
    if (start_win_idx != other.start_win_idx) {
      return false;
    }
    for (int i = 0; i < (int)level_runs.size(); i++) {
      if (level_runs[i].size() != other.level_runs[i].size()) {
        return false;
      }
    }
    return true;
  }
};

struct DynamicCompactioner {
  const double slope = 0.000717;
  const double intercept = 2664.67560;

  const double stall_speed = 16 * (1<<20);

  const double est_update_time = 5;
  const double est_range_lookup_time_perrun = 4;
  
  SearchTree search_tree;
  int search_depth = 5;
  uint32_t l0_slowdown = 1000;
  uint32_t buffer_size;
  int key_size;
  int val_size;
  TreeState* head_state = nullptr, *tail_state = nullptr;

  DynamicCompactioner(const std::vector<SearchNode>& windows, uint32_t buffsize, int ksize, int vsize):
    search_tree(windows),
    buffer_size(buffsize),
    key_size(ksize),
    val_size(vsize) {}

  double evaluate_compaction_microsec(uint64_t compaction_size) const {
    return slope * compaction_size * 2 + intercept; // in us
  }

  // std::thread search_thread;

  // a compaction reducing #sorted runs can be beneficial to all the operations after this window
  // 1. reward: range_lookup_nums * reduced_runs
  // 2. cost: compaction time causing write stall
  // return reward - cost
  double get_rewards_and_new_state(const TreeState& tree_state, const DynAction& act, TreeState& new_state) {
    // 1. reduced runs
    uint32_t reduced_runs = 0;
    if (act.merged_runs.size() > 0) {
      reduced_runs += act.merged_runs.size() - 1;
    }
    double stall_time = (double)(key_size + val_size) / stall_speed * 1e6; // us
    get_new_state(tree_state, act, new_state);
    // 2. write stall time
    if (reduced_runs > 0) {
      uint64_t slow_down_time = 0;
      if (new_state.level_runs[0].size() > l0_slowdown) {
        int slow_down_win_nums = new_state.level_runs[0].size() - l0_slowdown;
        for (int i = new_state.start_win_idx - 1, j = 0; j < slow_down_win_nums; i--, j++) {
          slow_down_time += search_tree.at(i).update_nums * stall_time;
        }
      }
      return reduced_runs * tree_state.merged_sketch.range_lookup_nums * est_range_lookup_time_perrun - slow_down_time;
    }
    return 0;
  }

  void get_new_state(const TreeState& tree_state, const DynAction& act, TreeState& new_state) {
    new_state.level_runs = tree_state.level_runs;
    new_state.start_win_idx = tree_state.start_win_idx;
    // gather the deleted run at the same level
    std::unordered_map<int, std::vector<int>> deleted_runs;
    for (auto& run : act.merged_runs) {
      deleted_runs[run.first].push_back(run.second);
    }
    uint64_t size = 0;
    for (auto& run : deleted_runs) {
      std::sort(run.second.begin(), run.second.end());
      for (int i = run.second.size() - 1; i >= 0; i--) {
        size += new_state.level_runs[run.first][run.second[i]];
        new_state.level_runs[run.first].erase(new_state.level_runs[run.first].begin() + run.second[i]);
      }
    }
    if (size > 0) {
      new_state.level_runs[act.target_level].push_back(size);
    }
    if (act.compaction_size > 0) {
      uint64_t compaction_time = evaluate_compaction_microsec(act.compaction_size);
      uint64_t window_time = 0;
      while (window_time < compaction_time) {
        auto window = search_tree.at(new_state.start_win_idx);
        window_time += estimate_window_micro(window, stall_speed,
          est_update_time, est_range_lookup_time_perrun,
          l0_slowdown, new_state.level_runs[0].size() >= l0_slowdown,
          key_size, val_size
        );
        new_state.merged_sketch.PopFront(window);
        new_state.start_win_idx ++;
        new_state.level_runs[0].push_back(buffer_size);
      }
    } else {
      new_state.start_win_idx = tree_state.start_win_idx + 1;
      new_state.merged_sketch.PopFront(search_tree.at(new_state.start_win_idx));
      new_state.level_runs[0].push_back(buffer_size);
    }
  }

  void pause_search() {}

  void start_search() {}

  TreeState Tick(const TreeState& tree_state) {
    search_tree.Tick();
    if (tree_state.start_win_idx + 1 == head_state->best_nxt_state->start_win_idx) {
      auto ret = *head_state->best_nxt_state;
      head_state = head_state->best_nxt_state.get();
      return ret;
    }
    auto ret = tree_state;
    ret.start_win_idx ++;
    ret.level_runs[0].push_back(buffer_size);
    return ret;
  }

  bool CheckValid(const TreeState& current_state) {
    return current_state.Eq(*head_state);
  }
  
  void search_layer(TreeState& tree_state, int depth) {
    if (depth > this->search_depth || tree_state.start_win_idx >= (int)search_tree.windows.size()) {
      return;
    }
    tree_state.best_action.reward = -1;
    auto actions = tree_state.EnumerateValidAction();
    for (auto& action : actions) {
      TreeState new_state;
      action.reward = get_rewards_and_new_state(tree_state, action, new_state);
      // search_layer(new_state, depth + new_state.start_win_idx - tree_state.start_win_idx);
      search_layer(new_state, depth + 1);
      double total_reward = action.reward + new_state.best_action.reward;
      if (total_reward > tree_state.best_action.reward) {
        tree_state.best_action = action;
        tree_state.best_nxt_state = std::make_shared<TreeState>(new_state);
      }
    }
  }
  // temporary function
  void SearchAll(TreeState& current_state) {
    search_layer(current_state, 0);
    head_state = new TreeState(current_state);
    auto state = head_state;
    while (state->best_nxt_state.get() != nullptr) {
      state = state->best_nxt_state.get();
    }
    tail_state = state;
  }
  void SearchOne() {
    // traverse to the bottom
    TreeState* state = tail_state;
    search_layer(*state, 0);
    while (state->best_nxt_state.get() != nullptr) {
      state = state->best_nxt_state.get();
    }
    tail_state = state;
  }

  void Clear() {
    // clear the searched state
    head_state = nullptr;
    tail_state = nullptr;
  }
};

}