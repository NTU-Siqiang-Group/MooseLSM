build/db_bench -numdistinct=5000000 -benchmarks=overwrite,stats -write_buffer_size=2097152 -level0_slowdown_writes_trigger=8 -level0_stop_writes_trigger=12 \
-compaction_style=4 -use_existing_db=true \
-run_numbers='1,3,3,3,2,2' -level_capacities='2097152,4893354,4893354,4893354,34253482,34253482,34253482,239774378,239774378,239774378,2157969408,2157969408,10789847040,10789847040' \
-num_levels=64 -value_size=1000 -key_size=24 -compression_type=none -num=11000000 -db=/tmp/db -progress_reports=false >> benchmark.log 2>&1

# build/db_bench -benchmarks=fillrandom,stats -write_buffer_size=2097152 \
# 	-level0_slowdown_writes_trigger=8 -level0_stop_writes_trigger=12 \
# 	-num_levels=6 -value_size=1000 -key_size=24 \
# 	-max_bytes_for_level_base=20971520 -compression_type=none -progress_reports=false \
# 	-num=11000000 -db=/tmp/db -numdistinct=5000000 >> benchmark.log 2>&1