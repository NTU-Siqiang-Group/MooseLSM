#include "rocksdb/dyncompactionv4.h"

#include <algorithm>
#include <cmath>
#include <omp.h>

namespace DynamicLookForward {
using DynCompaction::TreeState;
using DynCompaction::DynAction;
using DynCompaction::DynamicCompactioner;

std::pair<int, int> FindBestMC(const TreeState& latest_state, int64_t buffer_size, 
    int r, int u, int p, double wait_io, int mc_search_len, int remaining_window_cnt, double parallel_factor); 
}