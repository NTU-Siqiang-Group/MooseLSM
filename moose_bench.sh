# fill up with 110M records and then do 1M random seeks (range lookup) with bg writes
logfile=benchmark_update.log

# build/db_bench -numdistinct=5000000 -benchmarks=overwrite,stats,levelstats -write_buffer_size=2097152 -level0_slowdown_writes_trigger=8 -level0_stop_writes_trigger=12 \
# -compaction_style=4 -use_existing_db=false \
# -run_numbers='1,3,3,3,2,2' -level_capacities='2097152,4893354,4893354,4893354,34253482,34253482,34253482,239774378,239774378,239774378,2157969408,2157969408,10789847040,10789847040' \
# -num_levels=64 -value_size=1000 -key_size=24 -compression_type=none -num=11000000 -db=/tmp/db -progress_reports=false >> $logfile 2>&1

# build/db_bench -numdistinct=5000000 -benchmarks=seekrandomwhilewriting,stats,levelstats -write_buffer_size=2097152 -level0_slowdown_writes_trigger=8 -level0_stop_writes_trigger=12 \
# -compaction_style=4 -use_existing_db=true \
# -run_numbers='1,3,3,3,2,2' -level_capacities='2097152,4893354,4893354,4893354,34253482,34253482,34253482,239774378,239774378,239774378,2157969408,2157969408,10789847040,10789847040' \
# -num_levels=64 -value_size=1000 -key_size=24 -compression_type=none -num=1000000 -db=/tmp/db -progress_reports=false >> $logfile 2>&1

# rm -rf /tmp/db
# mkdir /tmp/db

# build/db_bench -numdistinct=5000000 -benchmarks=overwrite,stats,levelstats -write_buffer_size=2097152 \
# 	-level0_slowdown_writes_trigger=8 -level0_stop_writes_trigger=12 -use_existing_db=false \
# 	-num_levels=6 -value_size=1000 -key_size=24 -target_file_size_base=10485760 \
# 	-max_bytes_for_level_base=20971520 -compression_type=none -progress_reports=false \
# 	-num=11000000 -db=/tmp/db >> $logfile 2>&1

build/db_bench -numdistinct=5000000 -benchmarks=seekrandomwhilewriting,stats,levelstats -write_buffer_size=2097152 \
	-level0_slowdown_writes_trigger=8 -level0_stop_writes_trigger=12 -use_existing_db=true \
	-num_levels=6 -value_size=1000 -key_size=24 -target_file_size_base=10485760 \
	-max_bytes_for_level_base=20971520 -compression_type=none -progress_reports=false \
	-num=1000000 -db=/tmp/db >> $logfile 2>&1