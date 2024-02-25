#include "drogon/HttpController.h"
#include "gflags/gflags.h"
// #include "rocksdb/db.h"

#include <iostream>

DEFINE_int32(port, 8080, "port to listen on");

class KvServerCtrl : public drogon::HttpController<KvServerCtrl, false> {
 public:
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
    auto value = "value";
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
    auto resp = drogon::HttpResponse::newHttpJsonResponse({{ "status", "ok" }});
    callback(resp);
  }
  // rocksdb::DB* db;
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