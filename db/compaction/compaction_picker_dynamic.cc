#include "db/compaction/compaction_picker_dynamic.h"
#include "rocksdb/advanced_options.h"
// #include "db/compaction/dynamic_state.h"
#include "logging/logging.h"

#include <string>
#include <unordered_map>
#include <iostream>
#include <chrono>

namespace ROCKSDB_NAMESPACE {
bool DynamicCompactionPicker::NeedsCompaction(
    const VersionStorageInfo* vstorage) const {
  return true;
}

namespace {
class DynamicCompactionBuilder {
 public:
  DynamicCompactionBuilder(const std::string& cf_name, VersionStorageInfo* vstorage,
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
                        mutable_db_options_(mutable_db_options) {}
  // void GetCurrentState(DynCompactionV2::TreeState& state) {
  //   state.level_runs.resize(ioptions_.num_levels);
  //   for (int i = 0; i < ioptions_.num_levels; i++) {
  //     auto& files = vstorage_->LevelFiles(i);
  //     for (auto& file : files) {
  //       if (!file->being_compacted) {
  //         state.level_runs[i].push_back(file->fd.file_size);
  //         state.total_runs ++;
  //       }
  //     }
  //   }
  // }

  void GetCurrentStateForCompactedAction(DynCompactionV3::TreeState& state) {
    state.level_runs.resize(ioptions_.num_levels);
    int max_level_runs = 0;
    for (int i = 0; i < ioptions_.num_levels; i++) {
      auto& files = vstorage_->LevelFiles(i);
      for (auto& file : files) {
        if (!file->being_compacted) {
          state.level_runs[i].push_back(file->fd.file_size);
          state.total_runs ++;
        }
      }
      if (state.level_runs[i].size() > max_level_runs) {
        max_level_runs = state.level_runs[i].size();
      }
      // sort level_runs[i], desc
      if (i > 0) {
        std::sort(state.level_runs[i].begin(), state.level_runs[i].end(), std::greater<uint64_t>());
      }
    }
    state.max_level_runs = max_level_runs;
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

  // Compaction* pick_major_compactions(const DynCompactionV2::DynActionV2& act) {
  //   start_level_ = 0;
  //   output_level_ = act.target_level;
  //   int input_end_level = act.target_level - 1;
  //   if (act.end_level_end_idx == -1) {
  //     compaction_inputs_.resize(act.target_level);
  //   } else {
  //     compaction_inputs_.resize(act.target_level + 1);
  //     input_end_level += 1;
  //   }
  //   for (int i = 0; i <= input_end_level; i++) {
  //     auto files = vstorage_->LevelFiles(i);
  //     // sort files in desc order
  //     std::sort(files.begin(), files.end(), [](FileMetaData* a, FileMetaData* b) {
  //       return a->fd.file_size > b->fd.file_size;
  //     });
  //     for (int j = 0; j < (int)files.size(); j++) {
  //       if (files[j]->being_compacted) {
  //         // should not happen
  //         return nullptr;
  //       }
  //       if (act.end_level_end_idx >= 0 && i == input_end_level && j > act.end_level_end_idx) {
  //         break;
  //       }
  //       compaction_inputs_[i].files.push_back(files[j]);
  //     }
  //     compaction_inputs_[i].level = i;
  //   }
  //   auto c = new Compaction(
  //     vstorage_, ioptions_, mutable_cf_options_, mutable_db_options_,
  //     std::move(compaction_inputs_), output_level_,
  //     /* max file size */100UL * (1<<30),
  //     mutable_cf_options_.max_compaction_bytes,
  //     GetPathId(ioptions_, mutable_cf_options_, output_level_),
  //     GetCompressionType(vstorage_, mutable_cf_options_, output_level_,
  //                        vstorage_->base_level()),
  //     GetCompressionOptions(mutable_cf_options_, vstorage_, output_level_),
  //     Temperature::kUnknown,
  //     /* max_subcompactions */ 0, std::move(grandparents_), is_manual_,
  //     /* trim_ts */ "", /* start_level_score*/ 1, false /* deletion_compaction */,
  //     /* l0_files_might_overlap */ true,
  //     compaction_reason_);
  //   return c;
  // }

  // Compaction* PickCompactionWithCompactedActions() {
  //   auto compactioner = mutable_cf_options_.comp_controller->compactioner;
  //   // 1. get the current state
  //   DynCompactionV2::TreeState state;
  //   GetCurrentStateForCompactedAction(state);
  //   state.InitCompactedActions();
  //   int win_idx = mutable_cf_options_.comp_controller->cur_win_idx.load();
  //   auto best_action = compactioner->GetBestActionWithCompactedActions(state, win_idx);
  //   ROCKS_LOG_INFO(ioptions_.info_log, "dynamic_state (%d): \n%s", win_idx, state.ToString().c_str());
  //   ROCKS_LOG_INFO(ioptions_.info_log, "best_action: %s", best_action.ToString().c_str());
  //   if (best_action.start_level < 0) {
  //     return nullptr;
  //   }
  //   if (best_action.is_major) {
  //     return pick_major_compactions(best_action);
  //   }
  //   start_level_ = best_action.start_level;
  //   output_level_ = best_action.target_level;

  //   if (best_action.create_new) {
  //     compaction_inputs_.resize(1);
  //   } else {
  //     compaction_inputs_.resize(2);
  //   }
  //   auto files = vstorage_->LevelFiles(start_level_);
  //   // sort files by size, desc
  //   std::sort(files.begin(), files.end(), [](FileMetaData* a, FileMetaData* b) {
  //     return a->fd.file_size > b->fd.file_size;
  //   });
  //   for (int i = 0; i < (int)files.size() && i <= best_action.start_level_end_idx; i++) {
  //     if (!files[i]->being_compacted) {
  //       compaction_inputs_[0].files.push_back(files[i]);
  //     }
  //   }
  //   if (compaction_inputs_[0].files.size() == 0) {
  //     return nullptr;
  //   }
  //   compaction_inputs_[0].level = start_level_;
  //   if (!best_action.create_new) {
  //     auto output_level_files = vstorage_->LevelFiles(output_level_);
  //     // find the smallest file at this level
  //     uint64_t min_size = UINT64_MAX;
  //     FileMetaData* min_file = nullptr;
  //     for (auto& file : output_level_files) {
  //       if (!file->being_compacted && file->fd.file_size < min_size) {
  //         min_size = file->fd.file_size;
  //         min_file = file;
  //       }
  //     }
  //     if (min_file != nullptr) {
  //       compaction_inputs_[1].files.push_back(min_file);
  //       compaction_inputs_[1].level = output_level_;
  //     }
  //   }
  //   auto c = new Compaction(
  //     vstorage_, ioptions_, mutable_cf_options_, mutable_db_options_,
  //     std::move(compaction_inputs_), output_level_,
  //     /* max file size */100UL * (1<<30),
  //     mutable_cf_options_.max_compaction_bytes,
  //     GetPathId(ioptions_, mutable_cf_options_, output_level_),
  //     GetCompressionType(vstorage_, mutable_cf_options_, output_level_,
  //                        vstorage_->base_level()),
  //     GetCompressionOptions(mutable_cf_options_, vstorage_, output_level_),
  //     Temperature::kUnknown,
  //     /* max_subcompactions */ 0, std::move(grandparents_), is_manual_,
  //     /* trim_ts */ "", /* start_level_score*/ 1, false /* deletion_compaction */,
  //     /* l0_files_might_overlap */ true,
  //     compaction_reason_);
  //   return c;
  // }

  // Compaction* PickCompaction() {
  //   auto compactioner = mutable_cf_options_.comp_controller->compactioner;
  //   // 1. Get the current state
  //   DynCompactionV2::TreeState state;
  //   DynCompactionV2::DynAction best_action;
  //   int start_win_idx = mutable_cf_options_.comp_controller->cur_win_idx.load();
  //   GetCurrentState(state);
  //   best_action = compactioner->GetBestAction(state, start_win_idx);
  //   if (best_action.start_level < 0) {
  //     return nullptr;
  //   }
  //   // 4. form the compaction
  //   start_level_ = best_action.start_level;
  //   output_level_ = best_action.target_level;
  //   if (best_action.create_new) {
  //     compaction_inputs_.resize(1);
  //   } else {
  //     compaction_inputs_.resize(2);
  //   }
  //   auto files = vstorage_->LevelFiles(start_level_);
  //   for (auto& file : files) {
  //     compaction_inputs_[0].files.push_back(file);
  //   }
  //   compaction_inputs_[0].level = start_level_;
    
  //   if (!best_action.create_new) {
  //     auto output_level_files = vstorage_->LevelFiles(output_level_);
  //     // find the smallest file at this level
  //     uint64_t min_size = UINT64_MAX;
  //     FileMetaData* min_file = nullptr;
  //     for (auto& file : output_level_files) {
  //       if (file->fd.file_size < min_size) {
  //         min_size = file->fd.file_size;
  //         min_file = file;
  //       }
  //     }
  //     compaction_inputs_[1].files.push_back(min_file);
  //     compaction_inputs_[1].level = output_level_;
  //   }

  //   auto c = new Compaction(
  //     vstorage_, ioptions_, mutable_cf_options_, mutable_db_options_,
  //     std::move(compaction_inputs_), output_level_,
  //     /* max file size */100UL * (1<<30),
  //     mutable_cf_options_.max_compaction_bytes,
  //     GetPathId(ioptions_, mutable_cf_options_, output_level_),
  //     GetCompressionType(vstorage_, mutable_cf_options_, output_level_,
  //                        vstorage_->base_level()),
  //     GetCompressionOptions(mutable_cf_options_, vstorage_, output_level_),
  //     Temperature::kUnknown,
  //     /* max_subcompactions */ 0, std::move(grandparents_), is_manual_,
  //     /* trim_ts */ "", /* start_level_score*/ 1, false /* deletion_compaction */,
  //     /* l0_files_might_overlap */ true,
  //     compaction_reason_);
  //   return c;
  // }
 
  // Compaction* PickCompactionWithActionV3() {
  //   auto compactioner = mutable_cf_options_.comp_controller->compactioner;
  //   // 1. get the current state
  //   DynCompactionV2::TreeState state;
  //   GetCurrentStateForCompactedAction(state);
  //   state.InitCompactedActions();
  //   int win_idx = mutable_cf_options_.comp_controller->cur_win_idx.load();
  //   auto best_action = compactioner->GetBestActionV3(state, win_idx);
  //   ROCKS_LOG_INFO(ioptions_.info_log, "dynamic_state (%d): \n%s", win_idx, state.ToString().c_str());
  //   ROCKS_LOG_INFO(ioptions_.info_log, "best_action: %s, lookforward: %d\n", best_action.ToString().c_str(), compactioner->lookforward.load());
  //   if (best_action.start_level < 0) {
  //     return nullptr;
  //   }
  //   start_level_ = best_action.start_level;
  //   output_level_ = best_action.target_level;

  //   for (int i = start_level_; i <= output_level_; i++) {
  //     CompactionInputFiles input;
  //     auto files = vstorage_->LevelFiles(i);
  //     // sort files in desc order
  //     std::sort(files.begin(), files.end(), [](FileMetaData* a, FileMetaData* b) {
  //       return a->fd.file_size > b->fd.file_size;
  //     });
  //     for (int j = 0; j < (int)files.size(); j++) {
  //       if (files[j]->being_compacted) {
  //         // should not happen
  //         return nullptr;
  //       }
  //       if (best_action.removed_files[i][j]) {
  //         input.files.push_back(files[j]);
  //       }
  //     }
  //     if (input.files.size() > 0) {
  //       input.level = i;
  //       compaction_inputs_.push_back(std::move(input));
  //     }
  //   }
  //   if (compaction_inputs_.size() == 0) {
  //     return nullptr;
  //   }

  //   auto c = new Compaction(
  //     vstorage_, ioptions_, mutable_cf_options_, mutable_db_options_,
  //     std::move(compaction_inputs_), output_level_,
  //     /* max file size */100UL * (1<<30),
  //     mutable_cf_options_.max_compaction_bytes,
  //     GetPathId(ioptions_, mutable_cf_options_, output_level_),
  //     GetCompressionType(vstorage_, mutable_cf_options_, output_level_,
  //                        vstorage_->base_level()),
  //     GetCompressionOptions(mutable_cf_options_, vstorage_, output_level_),
  //     Temperature::kUnknown,
  //     /* max_subcompactions */ 0, std::move(grandparents_), is_manual_,
  //     /* trim_ts */ "", /* start_level_score*/ 1, false /* deletion_compaction */,
  //     /* l0_files_might_overlap */ true,
  //     compaction_reason_);
  //   return c;
  // }
  int get_lf_upper(const DynCompactionV3::TreeState& state) {
    int acc = 50;
    std::unordered_map<int, int> buckets;
    for (int i = 0; i < (int)state.level_runs.size(); i++) {
      for (int j = 0; j < (int)state.level_runs[i].size(); j++) {
        int b = std::floor(std::log2(std::max(1.0, state.level_runs[i][j] * 1.0 / 2 / (1<<20))));
        buckets[b] ++;
      }
    }
    for (auto& kv : buckets) {
      acc += (int)10.0 * std::sqrt(kv.first);
    }
    return acc;
  }

  Compaction* PickCompactionActionV3() {
    auto compactioner = mutable_cf_options_.comp_controller->compactioner;
    // 1. get the current state
    DynCompactionV3::TreeState state;
    GetCurrentStateForCompactedAction(state);
    // state.InitCompactedActions();
    state.EnumerateActions();
    // auto copy_state = state;
    int win_idx = mutable_cf_options_.comp_controller->cur_win_idx.load();
    auto best_action = compactioner->GetBestAction(state, win_idx);
    // auto best_action = compactioner->GetBestActionV2(state, win_idx);
    ROCKS_LOG_INFO(ioptions_.info_log, "dynamic_state (%d), lookforward (%d): \n%s", win_idx, compactioner->lookforward.load() ,state.ToString().c_str());
    ROCKS_LOG_INFO(ioptions_.info_log, "best_action: %s", best_action.ToString().c_str());
    // auto bv2 = compactioner->GetBestActionV2(copy_state, win_idx);
    // ROCKS_LOG_INFO(ioptions_.info_log, "V2 state:\n %s", copy_state.ToString().c_str());
    // ROCKS_LOG_INFO(ioptions_.info_log, "V2 best_action: %s", bv2.ToString().c_str());
    if (best_action.start_level < 0) {
      return nullptr;
    }
    start_level_ = best_action.start_level;
    output_level_ = best_action.target_level;

    for (int i = start_level_; i <= output_level_; i++) {
      CompactionInputFiles input;
      auto files = vstorage_->LevelFiles(i);
      // sort files in desc order
      if (i > 0) {
        std::sort(files.begin(), files.end(), [](FileMetaData* a, FileMetaData* b) {
          return a->fd.file_size > b->fd.file_size;
        });
      }
      for (int j = 0; j < (int)files.size(); j++) {
        if (files[j]->being_compacted) {
          // should not happen
          return nullptr;
        }
        if (best_action.removed_files[i][j]) {
          input.files.push_back(files[j]);
        }
      }
      if (input.files.size() > 0) {
        input.level = i;
        compaction_inputs_.push_back(std::move(input));
      }
    }
    if (compaction_inputs_.size() == 0) {
      return nullptr;
    }

    auto c = new Compaction(
      vstorage_, ioptions_, mutable_cf_options_, mutable_db_options_,
      std::move(compaction_inputs_), output_level_,
      /* max file size */100UL * (1<<30),
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
  CompactionInputFiles start_level_inputs_;
  std::vector<CompactionInputFiles> compaction_inputs_;
  CompactionInputFiles output_level_inputs_;
  std::vector<FileMetaData*> grandparents_;
  CompactionReason compaction_reason_ = CompactionReason::kUnknown;

  const MutableCFOptions& mutable_cf_options_;
  const ImmutableOptions& ioptions_;
  const MutableDBOptions& mutable_db_options_;
};
} // namespace

Compaction* DynamicCompactionPicker::PickCompaction(
    const std::string& cf_name, const MutableCFOptions& mutable_cf_options,
    const MutableDBOptions& mutable_db_options, VersionStorageInfo* vstorage,
    LogBuffer* log_buffer) {

  DynamicCompactionBuilder builder(cf_name, vstorage, this, log_buffer,
                               mutable_cf_options, ioptions_,
                               mutable_db_options);
  log_buffer->FlushBufferToLog();
  // auto compaction = builder.PickCompaction();
  // auto compaction = builder.PickCompactionWithCompactedActions();
  // auto compaction = builder.PickCompactionWithActionV3();
  auto compaction = builder.PickCompactionActionV3();
  return compaction;
}
} // namespace ROCKSDB_NAMESPACE