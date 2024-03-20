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

class KvServerCtrl : public drogon::HttpController<KvServerCtrl, false> {
 public:
  KvServerCtrl() {
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
  METHOD_LIST_END
 private:
  void get(const drogon::HttpRequestPtr& req,
             std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    auto key = req->getParameter("key");
    std::cout << "get " << key << std::endl;
    // TODO: get value from kv store
    std::string value;
    auto status = db->Get(rocksdb::ReadOptions(), key, &value);
    if (!status.ok()) {
      auto resp = drogon::HttpResponse::newHttpJsonResponse({{ "status", "fail" }});
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
    std::cout << "put " << key << " " << value << std::endl;
    // TODO: put key-value to kv store
    auto status = db->Put(rocksdb::WriteOptions(), key, value);
    if (!status.ok()) {
      auto resp = drogon::HttpResponse::newHttpJsonResponse({{ "status", "fail" }});
      callback(resp);
    } else {
      auto resp = drogon::HttpResponse::newHttpJsonResponse({{ "status", "ok" }});
      callback(resp);
    }
  }
  rocksdb::DB* db;
  rocksdb::Options opt;
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