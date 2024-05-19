#pragma once

#include <vector>
#include <math.h>

#include "rocksdb/filter_policy.h"
#include "rocksdb/options.h"
#include "rocksdb/table.h"

namespace ROCKSDB_NAMESPACE {
std::vector<double> MonkeyBpks(const uint64_t entry_num, const uint64_t filter_memory, const std::vector<uint64_t>& level_capacities,
    const uint64_t entry_size);
}