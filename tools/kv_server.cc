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
  void AddRecord(uint32_t ts, uint32_t used_time) {
    mtx_.lock();
    timestamps_.push_back(ts);
    metrics_.push_back(used_time);
    mtx_.unlock();
  }
  void GetAllLatency(std::vector<uint32_t>& ts, std::vector<uint32_t>& latencies) {
    mtx_.lock();
    ts = timestamps_;
    latencies = metrics_;
    mtx_.unlock();
  }
 private:
  std::string label_;
  std::vector<uint32_t> timestamps_;
  std::vector<uint32_t> metrics_;
  std::mutex mtx_;
};

int CountMicroSecond(std::chrono::steady_clock::time_point start) {
  auto end = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
}

class KvServerCtrl : public drogon::HttpController<KvServerCtrl, false> {
 public:
  KvServerCtrl(): get_latencies_("get"), put_latencies_("put") {
    opt = get_moose_options();
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
  METHOD_LIST_END
 private:
  void get(const drogon::HttpRequestPtr& req,
             std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    auto key = req->getParameter("key");
    // std::cout << "get " << key << std::endl;
    std::string value;
    auto start_sec = std::chrono::steady_clock::now();
    auto status = db->Get(rocksdb::ReadOptions(), key, &value);
    get_latencies_.AddRecord(std::time(nullptr), CountMicroSecond(start_sec));
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
    // std::cout << "sucessfully get" << std::endl;
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
    // std::cout << "put " << key << " " << value << std::endl;
    auto start_sec = std::chrono::steady_clock::now();
    auto status = db->Put(rocksdb::WriteOptions(), key, value);
    put_latencies_.AddRecord(std::time(nullptr), CountMicroSecond(start_sec));
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
    std::vector<uint32_t> get_ts, get_latencies;
    get_latencies_.GetAllLatency(get_ts, get_latencies);
    value_json["get"] = Json::Value();
    for (auto lt : get_latencies) {
      value_json["get"].append(lt);
    }
    value_json["get_ts"] = Json::Value();
    for (auto ts : get_ts) {
      value_json["get_ts"].append(ts);
    }
    std::vector<uint32_t> put_ts, put_latencies;
    put_latencies_.GetAllLatency(put_ts, put_latencies);
    value_json["put"] = Json::Value();
    for (auto lt : put_latencies) {
      value_json["put"].append(lt);
    }
    value_json["put_ts"] = Json::Value();
    for (auto ts : put_ts) {
      value_json["put_ts"].append(ts);
    }
    auto resp = drogon::HttpResponse::newHttpJsonResponse(value_json);
    callback(resp);
  }
  rocksdb::DB* db;
  rocksdb::Options opt;
  MetricManager get_latencies_, put_latencies_;
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