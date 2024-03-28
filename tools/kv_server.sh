#!/bin/bash
moosepath="/tmp/kvserver"
httpPrefix="http://127.0.0.1:8080/KvServerCtrl"

function start_server() {
  run_numbers="1,4,5,4"
  level_capacities="2097152,39845888,1075838976,21516779520"
  build/tools/kv_server --port=8080 --run_numbers=${run_numbers} --level_capacities=${level_capacities} --path=$moosepath > kv_server.log 2>&1 &
}

function get_key() {
  key=$1
  curl $httpPrefix/get\?key=${key}; echo
}

function set_key() {
  key=$1
  value=$2
  data="key=$key&value=$value"
  curl -X PUT $httpPrefix/put\?$data
}

function range_read() {
  key=$1
  value=$2
  data="key=$key&length=$value"
  curl $httpPrefix/range\?$data
}

function simple_test() {
  set_key "abc" "abc"; echo
  get_key "abc"; echo
  get_key "ab"; echo
}

function complex_test() {
  keys=()
  testNum=$1
  if [[ -z $testNum ]]; then
    testNum=100
  fi
  echo "testNum: $testNum"
  for (( i=1; i <= $testNum; i++ ))
  do
    # random key
    key=$(tr -dc A-Za-z0-9 </dev/urandom | head -c 512; echo)
    sleep 0.001
    set_key $key $key
    keys+=( $key )
  done
  for (( i=1; i <= $testNum; i++ ))
  do
    sleep 0.001
    get_key ${keys[$i]}
  done
  for (( i=1; i <= $testNum; i++ ))
  do
    key=$(tr -dc A-Za-z0-9 </dev/urandom | head -c 512; echo)
    get_key $key # not found
  done
  for (( i=1; i <= $testNum; i++))
  do
    sleep 0.001
    range_read ${keys[$i]} 5
  done
}

function empty_lookup() {
  testNum=$1
  if [[ -z $testNum ]]; then
    testNum=100
  fi
  echo "testNum: $testNum"
  for (( i=1; i <= $testNum; i++ ))
  do
    # random key
    key=$(tr -dc A-Za-z0-9 </dev/urandom | head -c 512; echo)
    sleep 0.001
    get_key $key
  done
}

function pure_write() {
  testNum=$1
  if [[ -z $testNum ]]; then
    testNum=100
  fi
  echo "testNum: $testNum"
  for (( i=1; i <= $testNum; i++ ))
  do
    # random key
    key=$(tr -dc A-Za-z0-9 </dev/urandom | head -c 512; echo)
    sleep 0.0001
    set_key $key $key
    keys+=( $key )
  done
}

function stop_and_clean_server() {
  ps -ef | grep "build/tools/kv_server" | awk '{print $2}' | xargs -I{} kill -9 {}
  rm -rf $moosepath
}

function get_latencies() {
  curl $httpPrefix/status; echo
}

$1 $2 $3