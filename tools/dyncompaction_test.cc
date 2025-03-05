#include "rocksdb/dyncompactionerv2.h"
#include "dynamic_test_util.h"

#include "gflags/gflags.h"
#include <algorithm>


DEFINE_string(query_file, "workloads/query2.dat", "query file");

const uint64_t buffer_size = 2 * (1<<20);

void updateState(DynCompactionV2::TreeState& state, const DynCompactionV2::DynActionV2& action) {
  if (action.start_level < 0) {
    return;
  }
  int start_level = action.start_level;
  int target_level = action.target_level;
  uint64_t total_size = 0;
  for (int i = start_level; i <= target_level; i++) {
    if (i == start_level && action.start_level_end_idx >= 0) {
      // erase the runs from [0, start_level_end_idx]
      total_size += std::accumulate(state.level_runs[i].begin(), state.level_runs[i].begin() + action.start_level_end_idx + 1, 0UL);
      state.level_runs[i].erase(state.level_runs[i].begin(), state.level_runs[i].begin() + action.start_level_end_idx + 1);
      state.total_runs -= action.start_level_end_idx + 1;
    } else if (i == target_level && action.end_level_end_idx >= 0) {
      // erase the runs from [0, end_level_end_idx]
      total_size += std::accumulate(state.level_runs[i].begin(), state.level_runs[i].begin() + action.end_level_end_idx + 1, 0UL);
      state.level_runs[i].erase(state.level_runs[i].begin(), state.level_runs[i].begin() + action.end_level_end_idx + 1);
      state.total_runs -= action.end_level_end_idx + 1;
    } else {
      total_size += std::accumulate(state.level_runs[i].begin(), state.level_runs[i].end(), 0UL);
      state.total_runs -= state.level_runs[i].size();
      state.level_runs[i].clear();
    }
  }
  // install the new run
  state.level_runs[target_level].push_back(total_size);
  state.total_runs += 1;
  int max_level_runs = INT_MIN;
  for (int i = 0; i < (int)state.level_runs.size(); i++) {
    max_level_runs = std::max(max_level_runs, (int)state.level_runs[i].size());
    // sort the run in desc order
    std::sort(state.level_runs[i].begin(), state.level_runs[i].end(), std::greater<uint64_t>());
  }
  state.max_level_runs = max_level_runs;
}

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  rocksdb::Options opt;
  opt.comp_controller = new rocksdb::AtomicCompactionController(buffer_size);
  opt.comp_controller->compactioner = new DynCompactionV2::DynamicCompactionerV2(2 * (1<<20), 100);
  WorkloadManager mng(opt.comp_controller, nullptr, 24, 1000, 16, 2 * (1<<20));
  mng.InitWorkloadFromFile(FLAGS_query_file);

  DynCompactionV2::TreeState cur_state, target_state;
  cur_state.level_runs.resize(4);
  cur_state.max_level_runs = 1;
  cur_state.total_runs = 1;
  cur_state.level_runs[3].push_back(20UL * (1<<30)); // initialized size 20GB
  target_state = cur_state;

  DynCompactionV2::DynActionV2 ongoing_action;
  int next_finished_idx = -1;

  std::vector<double> costs;
  std::vector<WorkloadManager::OpType> ops;
  for (int i = 0; i < (int)mng.workloads_.size(); i++) {
    auto window = mng.workloads_[i];
    std::vector<double> window_costs;
    std::vector<WorkloadManager::OpType> window_ops;

    double range_lookup_costs = 0;
    double point_lookup_costs = 0;
    double update_costs = 0;

    double range_lookup_percent = 0;
    double point_lookup_percent = 0;
    double update_percent = 0;
    while (window->cur_op_idx < (int)window->ops.size()) {
      auto op = window->ops[window->cur_op_idx];
      window_ops.push_back(op);
      if (op == WorkloadManager::OpType::RANGE_LOOKUP) {
        range_lookup_costs += cur_state.total_runs;
        window_costs.push_back(cur_state.total_runs);
        range_lookup_percent++;
      } else if (op == WorkloadManager::OpType::UPDATE) {
        window_costs.push_back(1024.0 / 4096);
        update_costs += 1024.0 / 4096;
        update_percent++;
      } else if (op == WorkloadManager::OpType::POINT_LOOKUP) {
        window_costs.push_back(0.01 * cur_state.total_runs + 1);
        point_lookup_percent++;
        point_lookup_costs += 0.01 * cur_state.total_runs + 1;
      }
      window->cur_op_idx ++;
    }
    cur_state.level_runs[0].push_back(buffer_size);
    target_state.level_runs[0].push_back(buffer_size);
    cur_state.total_runs ++;
    target_state.total_runs ++;

    if (cur_state.level_runs[0].size() > cur_state.max_level_runs) {
      cur_state.max_level_runs = cur_state.level_runs[0].size();
    }
    if (target_state.level_runs[0].size() > target_state.max_level_runs) {
      target_state.max_level_runs = target_state.level_runs[0].size();
    }
    
    if (next_finished_idx <= i) {
      // install the action
      cur_state = target_state;
      ongoing_action = DynCompactionV2::DynActionV2(); // reset
      next_finished_idx = -1;
    }
    // trigger a compaction
    if (ongoing_action.start_level < 0) {
      // no compaction is doing
      cur_state.InitCompactedActions();
      ongoing_action = opt.comp_controller->compactioner->GetBestActionWithCompactedActions(cur_state, i + 1);
      next_finished_idx = i + ongoing_action.estimate_finished_idx;
      target_state = cur_state;
      updateState(target_state, ongoing_action);
    }

    double avg_range_lookup_costs = range_lookup_costs / range_lookup_percent;
    double avg_point_lookup_costs = point_lookup_costs / point_lookup_percent;
    double avg_update_costs = update_costs / update_percent;
    std::cout << "Window: " << i << "/" << mng.workloads_.size() << std::endl
      << "Current state: \n" << cur_state.ToString()
      << "Total Runs: " << cur_state.total_runs << std::endl
      << "Window ops: (" << update_percent << "," << range_lookup_percent << "," << point_lookup_percent << ")" << std::endl
      << "Window costs: (" << avg_update_costs << "," << avg_range_lookup_costs << "," << avg_point_lookup_costs << ")" << std::endl
      << "Action: " << ongoing_action.ToString() << std::endl
      << "----------------------------------------" << std::endl;
    // copy window costs to global costs
    costs.insert(costs.end(), window_costs.begin(), window_costs.end());
    ops.insert(ops.end(), window_ops.begin(), window_ops.end());  
  }

  double avg_range_lookup_costs = 0;
  double avg_point_lookup_costs = 0;
  double avg_update_costs = 0;
  int range_lookup_cnt = 0;
  int point_lookup_cnt = 0;
  int update_cnt = 0;
  for (int i = 0; i < (int)costs.size(); i++) {
    if (ops[i] == WorkloadManager::OpType::RANGE_LOOKUP) {
      avg_range_lookup_costs += costs[i];
      range_lookup_cnt++;
    } else if (ops[i] == WorkloadManager::OpType::UPDATE) {
      avg_update_costs += costs[i];
      update_cnt++;
    } else if (ops[i] == WorkloadManager::OpType::POINT_LOOKUP) {
      avg_point_lookup_costs += costs[i];
      point_lookup_cnt++;
    }
  }
  avg_range_lookup_costs /= range_lookup_cnt;
  avg_point_lookup_costs /= point_lookup_cnt;
  avg_update_costs /= update_cnt;
  std::cout << "Total ops: (" << update_cnt << "," << range_lookup_cnt << "," << point_lookup_cnt << ")" << std::endl
    << "Total costs: (" << avg_update_costs << "," << avg_range_lookup_costs << "," << avg_point_lookup_costs << ")" << std::endl;
  return 0;
}