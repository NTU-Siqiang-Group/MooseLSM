#!/bin/bash
dbpath=/tmp/rocksdb
# readratio=0.99
# writeratio=0.01

rratios=(0.1 0.2 0.3 0.4 0.5 0.6 0.7 0.8 0.9)
# rratios=(0.2)
for r in "${rratios[@]}"
do
  readratio=$r
  writeratio=$(python3 -c "print(1 - $r)")
  echo "rratio: $readratio, wratio: $writeratio ..."
  # sudo fstrim -v /tmp
  # rm -rf $dbpath/*
  # build/tools/arce_bench --compaction_style=leveling --db_path=$dbpath --read_ratio=$readratio --write_ratio=$writeratio \
  #   --output_file="${readratio}_leveling_ops.log" > "${readratio}_leveling.log" 2>&1

  # sudo fstrim -v /tmp
  rm -rf $dbpath/*
  build/tools/arce_bench --compaction_style=dynamic --db_path=$dbpath --read_ratio=$readratio --write_ratio=$writeratio \
    --output_file="${readratio}_dynamic_ops.log" > "${readratio}_dynamic.log" 2>&1
done