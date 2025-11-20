#!/bin/bash
# sudo fstrim -v /tmp
# rm -rf /tmp/db/*
# build/tools/arce_bench --compaction_style=leveling --read_ratio=0.5 --write_ratio=0.5 \
#   --output_file=arce.log > arce_bench.log 2>&1 &
# PID=$!
# echo "Process PID is: $PID"
# pidstat -r -p $PID 1 > leveling_pgfault_disk.log

sudo fstrim -v /tmp
rm -rf /tmp/db/*
build/tools/arce_bench --compaction_style=dynamic --read_ratio=0.5 --write_ratio=0.5 \
  --output_file=arce_dynamic.log > arce_bench_dynamic_default.log 2>&1 &
PID=$!
echo "Process PID is: $PID"
pidstat -r -p $PID 1 > arce_dynamic_pgfault_disk_default.log