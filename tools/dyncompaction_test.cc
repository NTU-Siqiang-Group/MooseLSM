#include "rocksdb/dyncompactionerv2.h"
#include "dynamic_test_util.h"

#include "gflags/gflags.h"
#include <algorithm>

DEFINE_string(query_file, "workloads/query2.dat", "query file");
DEFINE_int32(lookforward, 100, "lookforward");
DEFINE_bool(fixed_lookforward, true, "fixed lookforward");

#define log_base(x, base) (std::log(x) / std::log(base))

const uint64_t buffer_size = 2 * (1<<20);

void updateState(DynCompactionV2::TreeState& state, const DynCompactionV2::DynActionV3& action) {
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

int updateLookforward(DynCompactionV2::TreeState& current_state) {
  std::unordered_map<int, int> size_cnts;
  for (int i = 0; i < (int)current_state.level_runs.size(); i++) {
    for (int j = 0; j < (int)current_state.level_runs[i].size(); j++) {
      int64_t run_size = current_state.level_runs[i][j];
      int64_t bucket = std::floor(std::max(0.0, std::log2(run_size / buffer_size)));
      size_cnts[bucket] ++;
    }
  }
  int score = 0;
  for (auto& kv : size_cnts) {
    score += kv.first * log_base(kv.second, 1.1);
  }
  return 100 + score;
}

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  rocksdb::Options opt;
  opt.comp_controller = new rocksdb::AtomicCompactionController(buffer_size);
  opt.comp_controller->compactioner = new DynCompactionV2::DynamicCompactionerV2(2 * (1<<20), FLAGS_lookforward);
  WorkloadManager mng(opt.comp_controller, nullptr, 24, 1000, 16, 2 * (1<<20));
  mng.InitWorkloadFromFile(FLAGS_query_file, false);
  // mng.InitWorkloadFromFile(FLAGS_query_file, false);
  auto back_node = opt.comp_controller->compactioner->workload.windows.back();
  for (int i = 0; i < 500; i++) {
    opt.comp_controller->compactioner->workload.Append(back_node);
  }

  DynCompactionV2::TreeState cur_state, target_state;
  cur_state.level_runs.resize(4);
  cur_state.max_level_runs = 1;
  cur_state.total_runs = 1;
  cur_state.level_runs[3].push_back(20UL * (1<<30)); // initialized size 20GB
  target_state = cur_state;

  DynCompactionV2::DynActionV3 ongoing_action;
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
      ongoing_action = DynCompactionV2::DynActionV3(); // reset
      next_finished_idx = -1;
    }
    // trigger a compaction
    if (ongoing_action.start_level < 0) {
      // no compaction is doing
      cur_state.InitCompactedActions();
      ongoing_action = opt.comp_controller->compactioner->GetBestActionV3(cur_state, i + 1);
      next_finished_idx = i + ongoing_action.estimate_finished_idx;
      target_state = cur_state;
      updateState(target_state, ongoing_action);
    }
    if (!FLAGS_fixed_lookforward) {
      opt.comp_controller->compactioner->lookforward = updateLookforward(cur_state);
    }
    

    double avg_range_lookup_costs = range_lookup_costs / range_lookup_percent;
    double avg_point_lookup_costs = point_lookup_costs / point_lookup_percent;
    double avg_update_costs = update_costs / update_percent;
    std::cout << "Window: " << i << "/" << mng.workloads_.size() << std::endl
      << "Current state: \n" << cur_state.ToString()
      << "Total Runs: " << cur_state.total_runs << std::endl
      << "Window ops: (" << update_percent << "," << range_lookup_percent << "," << point_lookup_percent << ")" << std::endl
      << "Window costs: (" << avg_update_costs << "," << avg_range_lookup_costs << "," << avg_point_lookup_costs << ")" << std::endl
      << "Action: " << ongoing_action.ToString()
      << "Lookforward: " << opt.comp_controller->compactioner->lookforward << std::endl
      << "----------------------------------------" << std::endl;
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