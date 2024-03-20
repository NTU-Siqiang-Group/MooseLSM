#!/bin/bash
exe="build/tools/rw_test"

moosepath="/tmp/moosedb"
defaultpath="/tmp/defaultdb"

function test_moose() {
  echo "testing moose..."
  run_numbers="1,4,5,4"
  level_capacities="2097152,39845888,1075838976,21516779520"
  compaction_style="moose"
  # echo "    preparing db: $moosepath"
  # ${exe} --compaction_style=${compaction_style} --run_numbers=${run_numbers} --level_capacities=${level_capacities} --workload="prepare" --path=$moosepath
  # rm -rf ${moosepath}_backup
  # cp -r $moosepath ${moosepath}_backup
  cp -r ${moosepath}_backup $moosepath
  echo "    testing..."
  ${exe} --compaction_style=${compaction_style} --run_numbers=${run_numbers} --level_capacities=${level_capacities} --workload="test" --path=$moosepath
}

function test_default() {
  echo "testing default..."
  echo "    preparing db: $defaultpath"
  ${exe} --compaction_style="default" --workload="prepare" --path=$defaultpath
  rm -rf ${defaultpath}_backup
  cp -r $defaultpath ${defaultpath}_backup
  # echo "    testing..."
  # ${exe} --compaction_style="default" --workload="test" --path=$defaultpath
}

function clean() {
  echo "removing $1..."
  rm -rf $1
}

# clean $moosepath
# test_moose
clean $defaultpath
test_default