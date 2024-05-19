#include "rocksdb/monkey_filter.h"

namespace ROCKSDB_NAMESPACE {
inline double log_base(double x, double base) { return log(x) / log(base); }
std::vector<double> MonkeyBpks(const uint64_t entry_num, const uint64_t filter_memory,
   const std::vector<uint64_t>& level_capacities, const uint64_t entry_size) {
  auto L = level_capacities.size();
  uint64_t N = entry_num;
  std::vector<double> bpks;
  std::vector<uint64_t> level_cap_num(L);
  uint64_t sum = 0;
  for (size_t i = 1; i < level_capacities.size(); i++) {
    auto cap = level_capacities[i];
    level_cap_num[i] = cap / entry_size;
    sum += cap / entry_size;
  }
  double ln22 = log(2) * log(2);
  double A = 0.0;
  for (uint64_t i = 1; i < L; i++) {
    A += log((double)level_cap_num[i] / ln22) * level_cap_num[i];
  }
  double lmda = -(ln22 * filter_memory * 8 + A) / sum;
  lmda = std::exp(lmda);
  for (uint64_t i = 1; i < L; i++) {
    double pi = lmda * level_cap_num[i] / ln22;
    if (pi > 1 || pi < 0) {
      exit(0);
    }
    bpks.push_back(-log(pi) / ln22);
  }
  return bpks;
}
} // namespace ROCKSDB_NAMESPACE
