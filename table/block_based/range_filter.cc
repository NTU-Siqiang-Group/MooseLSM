#include "rocksdb/slice.h"
#include "rocksdb/filter_policy.h"
#include "table/block_based/filter_policy_internal.h"

namespace rocksdb {
DynamicRangeFilterBitsReader::DynamicRangeFilterBitsReader(const Slice& contents) {
  std::stringstream ss(contents.ToString());
  ss >> filter;
}
void DynamicRangeFilterBitsBuilder::AddKey(const Slice& key) {
  key_buffer.push_back(std::stoull(key.ToString()));
}

Slice DynamicRangeFilterBitsBuilder::Finish(std::unique_ptr<const char[]>* buf) {
  // std::sort(key_buffer.begin(), key_buffer.end());
  auto g = grafite::filter(key_buffer.begin(), key_buffer.end(), bpk);
  std::stringstream ss;
  ss << g;
  char* grafite_char = new char[ss.str().size()];
  memcpy(grafite_char, ss.str().c_str(), ss.str().size());
  buf->reset(grafite_char);
  Slice out(grafite_char, ss.str().size());
  return out;
}

const FilterPolicy* NewDynamicRangeFilter(double bpk, double bloom_bpk) {
  return new DynamicRangeFilter(bpk, bloom_bpk);
}


} // namespace rocksdb