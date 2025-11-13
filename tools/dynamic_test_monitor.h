#pragma once

#include <chrono>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <string>
#include <iostream>

#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/env.h>
#include <rocksdb/status.h>
#include <rocksdb/utilities/db_ttl.h>

#include "rocksdb/listener.h"

enum MonitorOpType : char {
  RANGE_LOOKUP = 0x0,
  UPDATE = 0x1,
  FLUSH = 0x2,
  COMPACTION_START = 0x3,
  COMPACTION_END = 0x4,
  WINDOW_START = 0x5,
  WINDOW_END = 0x6,
};

const std::unordered_map<MonitorOpType, std::string> opTypeToStr = {
  {RANGE_LOOKUP, "RANGE_LOOKUP"},
  {UPDATE, "UPDATE"},
  {FLUSH, "FLUSH"},
  {COMPACTION_START, "COMPACTION_START"},
  {COMPACTION_END, "COMPACTION_END"},
  {WINDOW_START, "WINDOW_START"},
  {WINDOW_END, "WINDOW_END"},
};

class DynamicTestListener : public rocksdb::EventListener {
 private:
  std::chrono::high_resolution_clock::time_point start_point;
  std::vector<uint64_t> compaction_end_ts;
  std::vector<uint64_t> compacted_bytes;
  std::vector<uint64_t> compaction_start_winidx;
  std::vector<uint64_t> compaction_end_winidx;
 public:
  void SetStartPoint(const decltype(start_point)& ts) {
    start_point = ts;
  }

  virtual void OnCompactionCompleted(rocksdb::DB* db, const rocksdb::CompactionJobInfo& ci) override {
    auto ts = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - start_point).count();
    compacted_bytes.push_back(ci.stats.total_input_bytes + ci.stats.total_output_bytes);
    compaction_end_ts.push_back(ts);
    auto* ctrler = db->GetOptions().comp_controller;
    int64_t winidx = ctrler->cur_win_num.load();
    compaction_end_winidx.push_back(winidx);
  }

  virtual void OnCompactionBegin(rocksdb::DB* db, const rocksdb::CompactionJobInfo& ci) {
    auto* ctrler = db->GetOptions().comp_controller;
    int64_t winidx = ctrler->cur_win_num.load();
    compaction_start_winidx.push_back(winidx);
  }

  void DisplayCompactionDetails() {
    std::cout << "Printing Compaction Details: " << std::endl;
    for (int i = 0; i < (int)compaction_end_ts.size(); i++) {
      std::cout << compaction_end_ts[i] << " " << compacted_bytes[i] << std::endl;
    }
    std::cout << "----------------------" << std::endl;
  }

  void DisplayCompactionIdx() {
    std::cout << "Printing Compaction WinIdx: " << std::endl;
    for (int i = 0; i < (int)compaction_start_winidx.size(); i++) {
      std::cout << compaction_start_winidx[i] << " ";
      if (i < compaction_end_winidx.size()) {
        std::cout << compaction_end_winidx[i] << " " << compacted_bytes[i];
      }
      std::cout << std::endl;
    }
    std::cout << "----------------------" << std::endl;
  }

  DynamicTestListener() {
    start_point = std::chrono::high_resolution_clock::now();
  }
};