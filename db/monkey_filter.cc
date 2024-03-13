#include "rocksdb/monkey_filter.h"

namespace ROCKSDB_NAMESPACE {
inline double log_base(double x, double base) { return log(x) / log(base); }
std::vector<double> MonkeyBpks(const uint64_t entry_num, const uint64_t filter_memory,
    const uint64_t L0_runs_num, const std::vector<uint64_t>& level_capacities,
    const uint64_t entry_size) {
  auto L = level_capacities.size();
  uint64_t N = entry_num;
  std::vector<double> bpks;
  std::vector<uint64_t> level_cap_num;
  uint64_t sum = 0;
  for (auto cap : level_capacities) {
    level_cap_num.push_back(cap / entry_size);
    sum += cap / entry_size;
  }
  double ln22 = log(2) * log(2);
  double A = 0.0;
  for (uint64_t i = 0; i < L; i++) {
    int ni = i == 0 ? L0_runs_num : 1;
    A += log((double)level_cap_num[i] / ln22 / ni) * level_cap_num[i];
  }
  double lmda = -(ln22 * filter_memory * 8 + A) / sum;
  lmda = std::exp(lmda);
  for (uint64_t i = 0; i < L; i++) {
    double pi = lmda * level_cap_num[i] / ln22 / (i == 0 ? L0_runs_num : 1);
    if (pi > 1 || pi < 0) {
      exit(0);
    }
    bpks.push_back(-log(pi) / ln22);
  }
  uint64_t used_mem = 0;
  for (uint64_t i = 0; i < L; i++) {
    used_mem += bpks[i] * level_cap_num[i] / 8;
  }
  return bpks;
}
} // namespace ROCKSDB_NAMESPACE
