#include "rocksdb/dyncompactionv4.h"

namespace DynCompactionV4 {
void DynamicCompactionerV4::get_win_acc_ios(int cur_total_runs,
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

void DynamicCompactionerV4::get_reward_for_action(DynAction& action, int total_runs, const std::vector<double>& acc_ios, 
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
      inc_w = std::max(0, idx + total_runs - c) * DynamicCompactionerV4::kStallCost * u;
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