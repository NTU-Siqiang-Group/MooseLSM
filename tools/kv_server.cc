#include "drogon/HttpController.h"
#include "gflags/gflags.h"
#include "rocksdb/db.h"
#include "rocksdb/db.h"
#include "rocksdb/filter_policy.h"
#include "rocksdb/options.h"
#include "rocksdb/monkey_filter.h"
#include "util/string_util.h"
#include "rocksdb/monkey_filter.h"
#include "rocksdb/options.h"
#include "rocksdb/listener.h"
#include "rocksdb/statistics.h"

#include <chrono>
#include <iostream>
#include <numeric>
#include <gflags/gflags.h>
#include <random>

DEFINE_int32(port, 8080, "port to listen on");
DEFINE_string(level_capacities, "", "Comma-separated list of level capacities");
DEFINE_string(run_numbers, "", "Comma-separated list of run numbers");
DEFINE_int32(bpk, 5, "Bits per key for filter");
DEFINE_int32(kvsize, 1024, "Size of key-value pair");
DEFINE_string(compaction_style, "default", "Compaction style");
DEFINE_string(path, "/tmp/kvserver", "path of db");
DEFINE_uint32(max_return_stat, 500000, "");

int CountMicroSecond(std::chrono::steady_clock::time_point start) {
  auto end = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
}

class Clock {
 private:
  decltype(std::chrono::steady_clock::now()) start = std::chrono::steady_clock::now();
 public:
  uint64_t Cur() const {
    return CountMicroSecond(start);
  }
};

rocksdb::Options get_moose_options() {
  rocksdb::Options options;
  options.compaction_style = rocksdb::kCompactionStyleMoose;
  options.create_if_missing = true;
  options.write_buffer_size = 2 << 20;
  options.level_compaction_dynamic_level_bytes = false;
  
  std::vector<std::string> split_st =
      rocksdb::StringSplit(FLAGS_level_capacities, ',');
  std::vector<uint64_t> level_capacities;
  for (auto& s : split_st) {
    level_capacities.push_back(std::stoull(s));
  }
  split_st = rocksdb::StringSplit(FLAGS_run_numbers, ',');
  std::vector<int> run_numbers;
  for (auto& s : split_st) {
    run_numbers.push_back(std::stoi(s));
  }
  std::vector<uint64_t> physical_level_capacities;
  for (int i = 0; i < (int)run_numbers.size(); i++) {
    uint64_t run_size = level_capacities[i] / run_numbers[i];
    for (int j = 0; j < (int)run_numbers[i]; j++) {
      physical_level_capacities.push_back(run_size);
    }
  }
  options.level_capacities = physical_level_capacities;
  options.run_numbers = run_numbers;
  options.num_levels = std::accumulate(run_numbers.begin(), run_numbers.end(), 0);

  uint64_t entry_num = std::accumulate(options.level_capacities.begin(), options.level_capacities.end(), 0UL) / FLAGS_kvsize;
  uint64_t filter_memory = entry_num * FLAGS_bpk / 8;

  auto bpks = rocksdb::MonkeyBpks(entry_num, filter_memory, options.run_numbers[0], options.level_capacities, FLAGS_kvsize);
  // display options
  std::cout << "level capacities: " << std::endl;
  for (auto lvl_cap : options.level_capacities) {
    std::cout << "  " << lvl_cap << std::endl;
  }
  std::cout << "run numbers: " << std::endl;
  for (auto rn : options.run_numbers) {
    std::cout << "  " << rn << std::endl;
  }
  std::cout << "bpks: " << std::endl;
  for (auto bpk : bpks) {
    std::cout << "  " << bpk << std::endl;
  }
  
  auto table_options = options.table_factory->GetOptions<rocksdb::BlockBasedTableOptions>();
  table_options->filter_policy.reset(rocksdb::NewMonkeyFilterPolicy(bpks));

  return options;
}

class MetricManager {
 public:
  MetricManager(std::string label): label_(label) {}
  void AddRecord(uint64_t ts, uint64_t used_time) {
    mtx_.lock();
    timestamps_.push_back(ts);
    metrics_.push_back(used_time);
    mtx_.unlock();
  }
  void GetAllMetric(std::vector<uint64_t>& ts, std::vector<uint64_t>& ret) {
    mtx_.lock();
    ts = timestamps_;
    ret = metrics_;
    mtx_.unlock();
  }
  uint64_t GetSize() const {
    return timestamps_.size();
  }
 private:
  std::string label_;
  std::vector<uint64_t> timestamps_;
  std::vector<uint64_t> metrics_;
  std::mutex mtx_;
};

class CompactionListener : public rocksdb::EventListener {
 public:
  CompactionListener(MetricManager* out): compaction_times_(out) {}
  virtual void OnCompactionCompleted(rocksdb::DB* db, const rocksdb::CompactionJobInfo& info) override {
    auto ts = std::time(nullptr);
    compaction_times_->AddRecord(ts, info.stats.elapsed_micros);
  }
 private:
  std::shared_ptr<MetricManager> compaction_times_;
};


class KvServerCtrl : public drogon::HttpController<KvServerCtrl, false> {
 public:
  KvServerCtrl(): get_latencies_("get"), put_latencies_("put") {
    opt = get_moose_options();
    opt.listeners.emplace_back(new CompactionListener(&compaction_time_));
    opt.statistics = rocksdb::CreateDBStatistics();
    auto status = rocksdb::DB::Open(opt, "/tmp/kvserver", &db);
    if (!status.ok()) {
      std::cout << "fail to start db" << std::endl;
      exit(1);
    }
  }
  ~KvServerCtrl() {
    db->Close();
    delete db;
  }
  METHOD_LIST_BEGIN
  METHOD_ADD(KvServerCtrl::get, "/get", drogon::Get);
  METHOD_ADD(KvServerCtrl::put, "/put", drogon::Put);
  METHOD_ADD(KvServerCtrl::status, "/status", drogon::Get);
  METHOD_ADD(KvServerCtrl::range, "/range", drogon::Get);
  METHOD_LIST_END
 private:
  void range(const drogon::HttpRequestPtr& req,
             std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    auto start_key = req->getParameter("key");
    auto len = std::stoi(req->getParameter("length"));
    auto it = db->NewIterator(rocksdb::ReadOptions());
    auto start_sec = std::chrono::steady_clock::now();
    it->Seek(start_key);
    Json::Value ret;
    ret["key"] = Json::Value();
    ret["value"] = Json::Value();
    for (int i = 0; i < len && it->Valid(); i++) {
      ret["key"].append(it->key().ToString());
      ret["value"].append(it->value().ToString());
      it->Next();
    }
    range_latencies_.AddRecord(clk_.Cur(), CountMicroSecond(start_sec));
    delete it;
    ret["status"] = "ok";
    auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
    callback(resp);
  }
  void get(const drogon::HttpRequestPtr& req,
             std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    auto key = req->getParameter("key");
    std::string value;
    auto start_sec = std::chrono::steady_clock::now();
    auto status = db->Get(rocksdb::ReadOptions(), key, &value);
    get_latencies_.AddRecord(clk_.Cur(), CountMicroSecond(start_sec));
    if (!status.ok()) {
      Json::Value ret;
      ret["status"] = "fail";
      if (status.IsNotFound()) {
        ret["status"] = "not found";
      }
      auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
      callback(resp);
      return;
    }
    Json::Value value_json;
    value_json["value"] = value;
    value_json["status"] = "ok";
    auto resp = drogon::HttpResponse::newHttpJsonResponse(value_json);
    callback(resp);
  }
  void put(const drogon::HttpRequestPtr& req,
             std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    auto key = req->getParameter("key");
    auto value = req->getParameter("value");
    auto start_sec = std::chrono::steady_clock::now();
    auto status = db->Put(rocksdb::WriteOptions(), key, value);
    put_latencies_.AddRecord(clk_.Cur(), CountMicroSecond(start_sec));
    if (!status.ok()) {
      Json::Value ret;
      ret["status"] = "fail";
      auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
      callback(resp);
    } else {
      Json::Value ret;
      ret["status"] = "ok";
      auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
      callback(resp);
    }
  }
  void status(const drogon::HttpRequestPtr& req,
             std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    Json::Value value_json;

    auto start_ts = clk_.Cur() - FLAGS_max_return_stat;

    value_json["get"] = Json::Value();
    value_json["get_ts"] = Json::Value();
    FormatStatus(&get_latencies_, start_ts, value_json["get_ts"], value_json["get"]);

    value_json["put"] = Json::Value();
    value_json["put_ts"] = Json::Value();
    FormatStatus(&put_latencies_, start_ts, value_json["put_ts"], value_json["put"]);

    value_json["range"] = Json::Value();
    value_json["range_ts"] = Json::Value();
    FormatStatus(&range_latencies_, start_ts, value_json["range_ts"], value_json["range"]);

    value_json["compaction"] = Json::Value();
    value_json["compaction"]["ts"] = Json::Value();
    value_json["compaction"]["elapsed_time"] = Json::Value();
    FormatStatus(&compaction_time_, 0, value_json["compaction"]["ts"], value_json["compaction"]["elapsed_time"], 20);

    value_json["workload"]["get"] = get_latencies_.GetSize();
    value_json["workload"]["range"] = range_latencies_.GetSize();
    value_json["workload"]["write"] = put_latencies_.GetSize();

    value_json["stat"] = Json::Value();
    value_json["stat"]["compaction_number"] = compaction_time_.GetSize();
    uint64_t key_num = 0;
    db->GetIntProperty("rocksdb.estimate-num-keys", &key_num);
    value_json["stat"]["key_num"] = key_num;
    value_json["stat"]["flush_bytes"] = opt.statistics->getTickerCount(rocksdb::Tickers::FLUSH_WRITE_BYTES);
    int bloom_negative = opt.statistics->getTickerCount(rocksdb::Tickers::BLOOM_FILTER_USEFUL);
    int bloom_tp = opt.statistics->getTickerCount(rocksdb::Tickers::BLOOM_FILTER_FULL_TRUE_POSITIVE);
    int bloom_p = opt.statistics->getTickerCount(rocksdb::Tickers::BLOOM_FILTER_FULL_POSITIVE);
    value_json["stat"]["bloom_negative"] = opt.statistics->getTickerCount(rocksdb::Tickers::BLOOM_FILTER_USEFUL);
    value_json["stat"]["bloom_true_positive"] = opt.statistics->getTickerCount(rocksdb::Tickers::BLOOM_FILTER_FULL_TRUE_POSITIVE);
    value_json["stat"]["bloom_positive"] = opt.statistics->getTickerCount(rocksdb::Tickers::BLOOM_FILTER_FULL_POSITIVE);
    if (bloom_p - bloom_tp + bloom_negative != 0) {
      double fpr = (double)((double)bloom_p - bloom_tp) / ((double)bloom_p - bloom_tp + bloom_negative);
      value_json["stat"]["fpr"] = fpr;
    } else {
      value_json["stat"]["fpr"] = 0;
    }
    value_json["stat"]["total_compaction_read_bytes"] = opt.statistics->getTickerCount(rocksdb::Tickers::COMPACT_READ_BYTES);
    value_json["stat"]["total_compaction_write_bytes"] = opt.statistics->getTickerCount(rocksdb::Tickers::COMPACT_WRITE_BYTES);
    
    auto resp = drogon::HttpResponse::newHttpJsonResponse(value_json);
    callback(resp);
  }
  void FormatStatus(MetricManager* m, uint64_t start_ts, Json::Value& json_ts, Json::Value& json_metric, int max_return=0) {
    std::vector<uint64_t> ts_vec;
    std::vector<uint64_t> metrics;
    m->GetAllMetric(ts_vec, metrics);
    auto it = std::lower_bound(ts_vec.begin(), ts_vec.end(), start_ts);
    if (it == ts_vec.end()) {
      return;
    }
    uint64_t idx = it - ts_vec.begin();
    for (; idx < ts_vec.size() && (max_return == 0 || idx < max_return); idx ++) {
      json_ts.append(ts_vec[idx]);
      json_metric.append(metrics[idx]);
    }
  }
  Clock clk_;
  rocksdb::DB* db;
  rocksdb::Options opt;
  MetricManager get_latencies_, put_latencies_,
  range_latencies_ = MetricManager("range"),
  compaction_time_ = MetricManager("compaction_time");
};

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  drogon::app().
    addListener("0.0.0.0", FLAGS_port).
    registerController(std::make_shared<KvServerCtrl>()).
    setThreadNum(4).
    setLogLevel(trantor::Logger::kInfo).
    setLogPath("./").
    run();
  return 0;
}