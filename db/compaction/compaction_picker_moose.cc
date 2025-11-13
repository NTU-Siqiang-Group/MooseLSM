#include "db/compaction/compaction_picker_moose.h"
// #include <iostream>
#include "logging/logging.h"

namespace ROCKSDB_NAMESPACE {


bool MooseCompactionPicker::NeedsCompaction(
    const VersionStorageInfo* vstorage) const {
  return true;
}

namespace {
class MooseCompactionBuilder {
 public:
  MooseCompactionBuilder(const std::string& cf_name, VersionStorageInfo* vstorage,
                       CompactionPicker* compaction_picker,
                       LogBuffer* log_buffer,
                       const MutableCFOptions& mutable_cf_options,
                       const ImmutableOptions& ioptions,
                       const MutableDBOptions& mutable_db_options):
                        cf_name_(cf_name),
                        vstorage_(vstorage),
                        compaction_picker_(compaction_picker),
                        log_buffer_(log_buffer),
                        mutable_cf_options_(mutable_cf_options),
                        ioptions_(ioptions),
                        mutable_db_options_(mutable_db_options) {
    atomic_controller_ = mutable_cf_options_.comp_controller;
  }

  uint32_t GetPathId(
    const ImmutableCFOptions& ioptions,
    const MutableCFOptions& mutable_cf_options, int level) {
    uint32_t p = 0;
    assert(!ioptions.cf_paths.empty());

    // size remaining in the most recent path
    uint64_t current_path_size = ioptions.cf_paths[0].target_size;

    uint64_t level_size;
    int cur_level = 0;

    // max_bytes_for_level_base denotes L1 size.
    // We estimate L0 size to be the same as L1.
    level_size = mutable_cf_options.max_bytes_for_level_base;

    // Last path is the fallback
    while (p < ioptions.cf_paths.size() - 1) {
      if (level_size <= current_path_size) {
        if (cur_level == level) {
          // Does desired level fit in this path?
          return p;
        } else {
          current_path_size -= level_size;
          if (cur_level > 0) {
            if (ioptions.level_compaction_dynamic_level_bytes) {
              // Currently, level_compaction_dynamic_level_bytes is ignored when
              // multiple db paths are specified. https://github.com/facebook/
              // rocksdb/blob/main/db/column_family.cc.
              // Still, adding this check to avoid accidentally using
              // max_bytes_for_level_multiplier_additional
              level_size = static_cast<uint64_t>(
                  level_size * mutable_cf_options.max_bytes_for_level_multiplier);
            } else {
              level_size = static_cast<uint64_t>(
                  level_size * mutable_cf_options.max_bytes_for_level_multiplier *
                  mutable_cf_options.MaxBytesMultiplerAdditional(cur_level));
            }
          }
          cur_level++;
          continue;
        }
      }
      p++;
      current_path_size = ioptions.cf_paths[p].target_size;
    }
    return p;
  }
  
  bool NeedCompactionImpl() {
    // compute compaction score
    int total_run = 0;
    double score = 0.0;
    int start_logical_level = 0;
    // 1. check the compaction in L0
    auto& files_at_l0 = vstorage_->LevelFiles(0);
    int total_run_at_l0 = 0;
    for (auto f : files_at_l0) {
      if (f->being_compacted) {
        return false;
      }
      total_run_at_l0 ++;
    }
    if (total_run_at_l0 > atomic_controller_->size_ratios[0]) {
      score = (double)files_at_l0.size() / atomic_controller_->size_ratios[0];
    }
    total_run += total_run_at_l0;
    // 2. check other levels
    int prev_run_number = 1;
    // do not check the last level
    for (size_t i = 1; i < atomic_controller_->run_numbers.size() - 1; i++) {
      uint64_t cur_level_size = 0;
      uint64_t level_capacity = atomic_controller_->run_numbers[i] * atomic_controller_->run_sizes[i];
      int cur_level_run_number = 0;
      for (int j = 0; j < atomic_controller_->run_numbers[i]; j++) {
        auto& level_files = vstorage_->LevelFiles(prev_run_number + j);
        if (level_files.size() <= 0) {
          continue;
        }
        cur_level_run_number ++;
        for (auto& file : level_files) {
          if (file->being_compacted) {
            return false;
          }
          cur_level_size += file->fd.GetFileSize();
        }
      }
      total_run += cur_level_run_number;
      double cur_level_score = (double)cur_level_size / level_capacity;
      if (atomic_controller_->run_numbers[i] == atomic_controller_->size_ratios[i]) {
        cur_level_score = std::max(cur_level_score, (double)cur_level_run_number / atomic_controller_->run_numbers[i]);
      }
      if (cur_level_score >= 1.0) {
        // pick the bottommost level to compact
        score = cur_level_score;
        start_logical_level = i;
      }
      prev_run_number += atomic_controller_->run_numbers[i];
    }
    start_logical_level_ = start_logical_level;
    atomic_controller_->latest_run_num.store(total_run);
    // std::cout << "Compaction required: " << score << ", start from " << start_logical_level_ << std::endl;
    return score >= 1.0;
  }

  void SetupCompaction() {
    if (mutable_cf_options_.comp_controller->transit.load() > 0) {
      // compact the last level
      start_logical_level_ = mutable_cf_options_.comp_controller->transit.load();
      mutable_cf_options_.comp_controller->transit --;
      ROCKS_LOG_INFO(ioptions_.info_log, "transit level: %d\n", start_logical_level_);
    }
    uint64_t start_physical_level = std::accumulate(
      atomic_controller_->run_numbers.begin(),
      atomic_controller_->run_numbers.begin() + start_logical_level_,
      0UL
    );
    uint64_t end_physical_level = start_physical_level + atomic_controller_->run_numbers[start_logical_level_];
    // 1. collect all files at start level
    for (int i = start_physical_level; i < (int)end_physical_level; ++i) {
      const std::vector<FileMetaData*>& level_files = vstorage_->LevelFiles(i);
      CompactionInputFiles start_level_inputs;
      for (auto file : level_files) {
        if (file->being_compacted) {
          compaction_inputs_.clear();
          return;
        }
        start_level_inputs.files.push_back(file);
        if (start_physical_level == 0 && 
          start_level_inputs.files.size() == atomic_controller_->size_ratios[0]) {
          // shouldn't compact more files than the size we allow
          break;
        }
      }
      start_level_inputs.level = i;
      compaction_inputs_.push_back(start_level_inputs);
    }

    // 2. pick a physical level to output
    int target_logical_level = start_logical_level_ + 1;
    uint64_t target_start_physical_level = std::accumulate(
      atomic_controller_->run_numbers.begin(),
      atomic_controller_->run_numbers.begin() + target_logical_level,
      0UL
    );
    uint64_t target_end_physical_level = target_start_physical_level + atomic_controller_->run_numbers[target_logical_level];
    bool create_new_run = atomic_controller_->run_numbers[target_logical_level] == atomic_controller_->size_ratios[target_logical_level];
    int last_empty_run = -1;
    for (int i = target_end_physical_level - 1; i >= (int)target_start_physical_level; i--) {
      auto& level_files = vstorage_->LevelFiles(i);
      if (level_files.size() == 0) {
        last_empty_run = i;
        break;
      }
    }
    if (create_new_run) {
      // force to create a new run at the target level
      if (last_empty_run == -1) {
        // should not happen, just clean up the input
        compaction_inputs_.clear();
        return;
      }
      // no need to collect the files at target level
      output_level_ = last_empty_run;
    } else {
      // compact to the last active run
      if (last_empty_run == target_end_physical_level - 1) {
        // no need to collect the files at target level
        // the whole level is empty
        output_level_ = last_empty_run;
        return;
      }
      if (last_empty_run == -1) {
        // the whole level is full
        output_level_ = target_end_physical_level - 1;
        auto last_run_files = vstorage_->LevelFiles(output_level_);
        for (auto file : last_run_files) {
          if (file->being_compacted) {
            compaction_inputs_.clear();
            return;
          }
          output_level_inputs_.files.push_back(file);
        }
        output_level_inputs_.level = output_level_;
        return;
      }
      auto& prev_level_files = vstorage_->LevelFiles(last_empty_run + 1);
      uint64_t cur_run_size = 0;
      for (auto& file : prev_level_files) {
        if (file->being_compacted) {
          compaction_inputs_.clear();
          return;
        }
        cur_run_size += file->fd.GetFileSize();
      }
      if (cur_run_size >= atomic_controller_->run_sizes[target_logical_level]) {
        // no need to collect the files at target level
        // simply create a new sorted run
        output_level_ = last_empty_run;
        return;
      }
      output_level_ = last_empty_run + 1;
      output_level_inputs_.level = output_level_;
      for (auto file : prev_level_files) {
        if (file->being_compacted) {
          compaction_inputs_.clear();
          return;
        }
        output_level_inputs_.files.push_back(file);
      }
    }
  }

  Compaction* PickCompaction() {
    SetupCompaction();
    if (output_level_ == -1 || compaction_inputs_.size() == 0) {
      return nullptr;
    }
    if (output_level_inputs_.files.size() > 0) {
      compaction_inputs_.push_back(std::move(output_level_inputs_));
    }
    auto c = new Compaction(
      vstorage_, ioptions_, mutable_cf_options_, mutable_db_options_,
      std::move(compaction_inputs_), output_level_,
      /* max file size */10UL * (1<<20),
      mutable_cf_options_.max_compaction_bytes,
      GetPathId(ioptions_, mutable_cf_options_, output_level_),
      GetCompressionType(vstorage_, mutable_cf_options_, output_level_,
                         vstorage_->base_level()),
      GetCompressionOptions(mutable_cf_options_, vstorage_, output_level_),
      Temperature::kUnknown,
      /* max_subcompactions */ 0, std::move(grandparents_), is_manual_,
      /* trim_ts */ "", /* start_level_score*/ 1, false /* deletion_compaction */,
      /* l0_files_might_overlap */ true,
      compaction_reason_);
    return c;
  }
 private:
  const std::string& cf_name_;
  VersionStorageInfo* vstorage_;
  CompactionPicker* compaction_picker_;
  LogBuffer* log_buffer_;
  int start_level_ = -1;
  int output_level_ = -1;
  int parent_index_ = -1;
  int base_index_ = -1;
  double start_level_score_ = 0;
  bool is_manual_ = false;
  bool is_l0_trivial_move_ = false;
  // For Moose Compaction only
  int start_logical_level_ = 0;
  CompactionInputFiles start_level_inputs_;
  std::vector<CompactionInputFiles> compaction_inputs_;
  CompactionInputFiles output_level_inputs_;
  std::vector<FileMetaData*> grandparents_;
  CompactionReason compaction_reason_ = CompactionReason::kUnknown;

  const MutableCFOptions& mutable_cf_options_;
  const ImmutableOptions& ioptions_;
  const MutableDBOptions& mutable_db_options_;

  AtomicCompactionController* atomic_controller_ = nullptr;
};
} // namespace

Compaction* MooseCompactionPicker::PickCompaction(const std::string& cf_name,
                                     const MutableCFOptions& mutable_cf_options,
                                     const MutableDBOptions& mutable_db_options,
                                     VersionStorageInfo* vstorage,
                                     LogBuffer* log_buffer) {
  // Implementation of PickCompaction
  MooseCompactionBuilder builder(cf_name, vstorage, this, log_buffer,
                                  mutable_cf_options, ioptions_, mutable_db_options);
  if (!builder.NeedCompactionImpl()) {
    return nullptr;
  }
  return builder.PickCompaction();
}
} // namespace ROCKSDB_NAMESPACE