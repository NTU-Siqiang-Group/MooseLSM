#include "db/compaction/compaction_picker_moose.h"
#include "logging/logging.h"
#include <iostream>
#include <queue>

namespace ROCKSDB_NAMESPACE {
bool MooseCompactionPicker::NeedsCompaction(const VersionStorageInfo* vstorage) const {
  if (!vstorage->FilesMarkedForCompaction().empty()) {
    return true;
  }
  if (!vstorage->FilesMarkedForForcedBlobGC().empty()) {
    return true;
  }
  for (int i = 0; i <= vstorage->num_levels(); i++) {
    if (vstorage->CompactionScore(i) >= 1) {
      return true;
    }
  }
  return false;
}

namespace {
class MooseCompactionBuilder {
 public:
  MooseCompactionBuilder(const std::string& cf_name, VersionStorageInfo* vstorage,
                       CompactionPicker* compaction_picker,
                       LogBuffer* log_buffer,
                       const MutableCFOptions& mutable_cf_options,
                       const ImmutableOptions& ioptions,
                       const MutableDBOptions& mutable_db_options)
      : cf_name_(cf_name),
        vstorage_(vstorage),
        compaction_picker_(compaction_picker),
        log_buffer_(log_buffer),
        mutable_cf_options_(mutable_cf_options),
        ioptions_(ioptions),
        mutable_db_options_(mutable_db_options) {}
  Compaction* PickCompaction();
  void SetupInitialFiles();
  // return true if the compaction is specific for L0
  bool PickFileToCompact();
  bool PickFilesForL0(InternalKey* smallest, InternalKey* largest);
  bool PickFilesForLevel(InternalKey* smallest, InternalKey* largest);
  int ComputeLogicalLevel(int lvl) const;
  void Reset() {
    compaction_inputs_.clear();
    output_level_inputs_.files.clear();
    grandparents_.clear();
    start_level_ = -1;
    output_level_ = -1;
    parent_index_ = -1;
    base_index_ = -1;
    start_level_score_ = 0;
    is_manual_ = false;
    is_l0_trivial_move_ = false;
  }
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
  std::vector<CompactionInputFiles> compaction_inputs_;
  CompactionInputFiles output_level_inputs_;
  std::vector<FileMetaData*> grandparents_;
  CompactionReason compaction_reason_ = CompactionReason::kUnknown;

  const MutableCFOptions& mutable_cf_options_;
  const ImmutableOptions& ioptions_;
  const MutableDBOptions& mutable_db_options_;
  // Pick a path ID to place a newly generated file, with its level
  static uint32_t GetPathId(const ImmutableCFOptions& ioptions,
                            const MutableCFOptions& mutable_cf_options,
                            int level);
  static const int kMinFilesForIntraL0Compaction = 4;
};

struct MergeGroup {
  std::unordered_map<FileMetaData*, int> files;
  std::vector<FileMetaData*> order_files;
  const Comparator* cmp;
  InternalKey smallest, largest;
  MergeGroup(const Comparator* comparator) : cmp(comparator) {}
  void AddFile(FileMetaData* f, int physical_level) {
    if (files.size() == 0) {
      smallest = f->smallest;
      largest = f->largest;
    } else {
      if (cmp->Compare(f->smallest.user_key(), smallest.user_key()) < 0) {
        smallest = f->smallest;
      }
      if (cmp->Compare(f->largest.user_key(), largest.user_key()) > 0) {
        largest = f->largest;
      }
    }
    files.insert({f, physical_level});
    order_files.push_back(f);
  }
  bool Empty() {
    return files.size() == 0;
  }
  bool IsIntersected(FileMetaData* f) {
    bool file_min_smaller_equal_than_min =
      !(cmp->Compare(f->smallest.user_key(), smallest.user_key()) > 0);
    bool file_max_larger_or_equal_than_max =
        !(cmp->Compare(f->largest.user_key(), largest.user_key()) < 0);
    if (file_min_smaller_equal_than_min && file_max_larger_or_equal_than_max) {
      return true;
    }
    bool file_min_larger_equal_than_min =
        !(cmp->Compare(f->smallest.user_key(), smallest.user_key()) < 0);
    bool file_max_smaller_equal_than_max =
        !(cmp->Compare(f->largest.user_key(), largest.user_key()) > 0);
    if (file_min_larger_equal_than_min && file_max_smaller_equal_than_max) {
      return true;
    }
    bool file_min_smaller_equal_than_max =
        !(cmp->Compare(f->smallest.user_key(), largest.user_key()) > 0);
    if (file_min_larger_equal_than_min && file_min_smaller_equal_than_max) {
      return true;
    }
    bool file_max_larger_equal_than_min =
        !(cmp->Compare(f->largest.user_key(), smallest.user_key()) < 0);
    if (file_max_larger_equal_than_min && file_max_smaller_equal_than_max) {
      return true;
    }
    return false;
  }
};

// uint64_t decodeKey(const std::string& key) {
//   uint64_t result = 0;
//   char* ptr = reinterpret_cast<char*>(&result);
//   int bytes = sizeof(result);
//   for (int i = 0; i < bytes; i++) {
//     ptr[i] = key.data()[bytes - i];
//   }
//   return result;
// }

// void PrintCompactionDetailInfo(const std::vector<CompactionInputFiles>& inputs) {
//   std::cout << "Compaction ready - last level : " << inputs.back().level << std::endl;
//   for (auto input : inputs) {
//     std::cout << "  Level " << input.level << ", file number: " << input.files.size() << std::endl;
//     uint64_t total_size = 0;
//     for (auto file : input.files) {
//       // std::cout << "    file #" << file->fd.GetNumber() << " " <<  decodeKey(file->smallest.user_key().ToString()) << " - " << decodeKey(file->largest.user_key().ToString()) << std::endl;
//       total_size += file->fd.file_size;
//     }
//     std::cout << "    total size: " << total_size << " bytes" << std::endl;
//     std::cout << std::endl;
//   }
// }

uint32_t MooseCompactionBuilder::GetPathId(
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


int MooseCompactionBuilder::ComputeLogicalLevel(int target_lvl) const {
  int total_lvl = 0;
  auto run_numbers = mutable_cf_options_.run_numbers;
  for (size_t i = 0; i < run_numbers.size(); i++) {
    total_lvl += run_numbers[i];
    if (target_lvl < total_lvl) {
      return i;
    }
  }
  // unlikely to reach here
  assert(false);
  return -1;
}

Compaction* MooseCompactionBuilder::PickCompaction() {
  SetupInitialFiles();
  if (compaction_inputs_.size() == 0) {
    return nullptr;
  }
  output_level_inputs_.level = output_level_;
  if (!output_level_inputs_.empty()) {
    compaction_inputs_.push_back(output_level_inputs_);
  }
  std::sort(compaction_inputs_.begin(), compaction_inputs_.end(),
    [](const CompactionInputFiles& a, const CompactionInputFiles& b) {
      return a.level < b.level;
  });
  compaction_reason_ = CompactionReason::kLevelMaxLevelSize;
  int output_logical_level = ComputeLogicalLevel(output_level_);
  uint64_t output_file_size = std::pow(2, output_logical_level - 1) * mutable_cf_options_.write_buffer_size;
  // uint64_t output_file_size = 10UL * (1<<20);
  // if (output_level_ >= 10) {
  //   PrintCompactionDetailInfo(compaction_inputs_);
  // }
  auto c = new Compaction(
      vstorage_, ioptions_, mutable_cf_options_, mutable_db_options_,
      std::move(compaction_inputs_), output_level_,
      output_file_size,
      mutable_cf_options_.max_compaction_bytes,
      GetPathId(ioptions_, mutable_cf_options_, output_level_),
      GetCompressionType(vstorage_, mutable_cf_options_, output_level_,
                         vstorage_->base_level()),
      GetCompressionOptions(mutable_cf_options_, vstorage_, output_level_),
      Temperature::kUnknown,
      /* max_subcompactions */ 0, std::move(grandparents_), is_manual_,
      /* trim_ts */ "", start_level_score_, false /* deletion_compaction */,
      /* l0_files_might_overlap */ start_level_ == 0 && !is_l0_trivial_move_,
      compaction_reason_);
  compaction_picker_->RegisterCompaction(c);
  vstorage_->ComputeCompactionScore(ioptions_, mutable_cf_options_);
  return c;
}

void MooseCompactionBuilder::SetupInitialFiles() {
  for (int i = 0; i < compaction_picker_->NumberLevels() - 1; i++) {
    // find the level that is most needed to be compacted
    // note that, the levels are sorted according to their scores.
    start_level_score_ = vstorage_->CompactionScore(i);
    start_level_ = vstorage_->CompactionScoreLevel(i);
    assert(i == 0 || start_level_score_ <= vstorage_->CompactionScore(i - 1));
    if (start_level_score_ >= 1) {
      if (PickFileToCompact()) {
        break;
      } else {
        Reset();
      }
    } else {
      // no need to compact
      break;
    }
  }
}

bool MooseCompactionBuilder::PickFileToCompact() {
  int target_logical_level = 0;
  InternalKey smallest, largest;
  if (start_level_ == 0) {
    if (!PickFilesForL0(&smallest, &largest)) {
      return false;
    }
  } else {
    if (!PickFilesForLevel(&smallest, &largest)) {
      return false;
    }
  }
  target_logical_level = ComputeLogicalLevel(start_level_) + 1;
  assert(target_logical_level < (int)mutable_cf_options_.run_numbers.size());
  int output_start_level = std::accumulate(
      mutable_cf_options_.run_numbers.begin(),
      mutable_cf_options_.run_numbers.begin() + target_logical_level, 0);
  int output_end_level = output_start_level + mutable_cf_options_.run_numbers[target_logical_level];
  for (int i = output_end_level - 1; i >= output_start_level; i--) {
    auto files = vstorage_->LevelFiles(i);
    uint64_t level_size = 0;
    uint64_t level_max_size = mutable_cf_options_.level_capacities[i];
    for (auto f : files) {
      if (f->being_compacted) {
        continue;
      }
      level_size += f->compensated_file_size;
    }
    if (level_size < level_max_size) {
      output_level_ = i;
      CompactionInputFiles input;
      input.level = i;
      std::vector<FileMetaData*> output_level_files;
      vstorage_->GetOverlappingInputs(i, &smallest, &largest, &input.files);
      if (!input.empty()) {
        compaction_picker_->ExpandInputsToCleanCut(cf_name_, vstorage_, &input);
      }
      for (auto f : input.files) {
        if (!f->being_compacted) {
          output_level_files.push_back(f);
        }
      }
      input.files = output_level_files;
      output_level_inputs_ = input;
      break;       
    }
  }
  return output_level_ > 0;
}

bool MooseCompactionBuilder::PickFilesForLevel(InternalKey* smallest, InternalKey* largest) {
  assert(start_level_ > 0);
  int logical_level = ComputeLogicalLevel(start_level_);
  int start_physical_level = std::accumulate(
      mutable_cf_options_.run_numbers.begin(),
      mutable_cf_options_.run_numbers.begin() + logical_level, 0);
  int end_physical_level = start_physical_level + mutable_cf_options_.run_numbers[logical_level];
  const auto comparator = vstorage_->InternalComparator()->user_comparator();
  auto cmp = [&](std::pair<FileMetaData*, uint32_t> left,
                 std::pair<FileMetaData*, uint32_t> right) {
    return comparator->Compare(left.first->smallest.user_key(),
                               right.first->smallest.user_key()) > 0;
  };
  std::priority_queue<std::pair<FileMetaData*, uint32_t>, std::vector<std::pair<FileMetaData*, uint32_t>>, decltype(cmp)> min_key_queue(cmp);
  for (int i = start_physical_level; i < end_physical_level; i++) {
    if (vstorage_->LevelFiles(i).size() > 0) {
      auto file = vstorage_->LevelFiles(i).at(0);
      min_key_queue.push({file, i});
    }
  }
  if (min_key_queue.size() == 0) {
    return false;
  }
  // make intersected files at logical level into group
  std::vector<std::shared_ptr<MergeGroup>> groups;
  std::vector<int> sst_idxs(end_physical_level, 0);
  auto current = std::make_shared<MergeGroup>(comparator);
  while (!min_key_queue.empty()) {
    // BFS
    auto f = min_key_queue.top().first;
    auto phy_lvl = min_key_queue.top().second;
    min_key_queue.pop();
    int idx = sst_idxs[phy_lvl];
    if (current->Empty() || current->IsIntersected(f)) {
      current->AddFile(f, phy_lvl);
    } else {
      groups.push_back(current);
      current.reset(new MergeGroup(comparator));
      current->AddFile(f, phy_lvl);
    }
    if ((int)vstorage_->LevelFiles(phy_lvl).size() > idx + 1) {
      // proceed the index point at phy_lvl
      auto new_file = vstorage_->LevelFiles(phy_lvl).at(idx + 1);
      min_key_queue.push({new_file, phy_lvl});
      sst_idxs[phy_lvl] ++;
    }
  }
  groups.push_back(current);
  // if (logical_level >= 3) {
  //   std::cout << "grouping logical level " << logical_level << " : " << groups.size() << std::endl;
  // }
  // randomly choose one group to compact
  int compact_idx = rand() % groups.size();
  auto merge_group = groups[compact_idx];
  *smallest = merge_group->smallest;
  *largest = merge_group->largest;
  std::unordered_map<int, CompactionInputFiles> comp_files;
  for (auto f : merge_group->order_files) {
    int phy_lvl = merge_group->files[f];
    if (!f->being_compacted) {
      if (comp_files.find(phy_lvl) == comp_files.end()) {
        CompactionInputFiles input;
        input.level = phy_lvl;
        comp_files[phy_lvl] = input;
      }
      comp_files[phy_lvl].files.push_back(f);
    }
  }
  for (auto it : comp_files) {
    auto& comp_input = it.second;
    compaction_inputs_.push_back(comp_input);
  }
  return true;
}

bool MooseCompactionBuilder::PickFilesForL0(InternalKey* smallest, InternalKey* largest) {
  // pick the oldest file to compact
  auto& level_files = vstorage_->LevelFiles(0);
  if (level_files.empty()) {
    return false;
  }
  compaction_inputs_.push_back({});
  compaction_inputs_.back().level = 0;
  for (auto it = level_files.rbegin(); it != level_files.rend(); ++it) {
    auto f = *it;
    if (f->being_compacted) {
      continue;
    }
    compaction_inputs_.back().files.push_back(f);
  }
  compaction_picker_->GetRange(compaction_inputs_[0], smallest, largest);
  return compaction_inputs_.back().files.size() >= 1;
}


} // namespace

Compaction* MooseCompactionPicker::PickCompaction(
    const std::string& cf_name, const MutableCFOptions& mutable_cf_options,
    const MutableDBOptions& mutable_db_options, VersionStorageInfo* vstorage,
    LogBuffer* log_buffer) {
  MooseCompactionBuilder builder(cf_name, vstorage, this, log_buffer,
                               mutable_cf_options, ioptions_,
                               mutable_db_options);
  return builder.PickCompaction();
}
} // namespace ROCKSDB_NAMESPACE