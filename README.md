# ArceKV optimized for In-memory service

## Pre-requisite
- cmake
- make
- gcc/g++
- gflags

## Build
```shell
mkdir -p build
cd build && cmake .. -CMAKE_BUILD_TYPE=Release
sudo make install -j
```

## Benchmark
This implementation optimizes the latency for in-memory key-value service (e.g., TikTok recommendation system). To run the benchmark, we mount a `tmpfs` and store the data in it:

```shell
sudo mount -t tmpfs -o size=100G tmpfs /tmp/rocksdb
```
To run the benchmark:
```shell
rm -rf /tmp/rocksdb/*
build/tools/arce_bench --compaction_style=dynamic --db_path=/tmp/rocksdb --read_ratio=0.1 --write_ratio=0.9 --output_file="0.1_dynamic_ops.log" > "0.1_dynamic.log" 2>&1

# rocksdb default
rm -rf /tmp/rocksdb/*
build/tools/arce_bench --compaction_style=leveling --db_path=/tmp/rocksdb --read_ratio=0.1 --write_ratio=0.9 --output_file="0.1_leveling_ops.log" > "0.1_leveling.log" 2>&1
```

Result on Ubuntu 22.04 with 13th Gen Intel(R) Core(TM) i9-13900K, 128GB RAM, and 1TB NVMe SSD:

| method | read_ratio | read latency (s) | write latency (s) | total latency (s) |
|-----------|:------------:|:------------:|:------------:|:---------:|
| ArceKV   | 0.1        | 98.372669  | 183.716458 | 282.09  |
| RocksDB  | 0.1        | 106.103891 | 702.745779 | 808.85  |
| ArceKV   | 0.2        | 145.888376 | 152.630936 | 298.52  |
| RocksDB  | 0.2        | 187.870437 | 510.525195 | 698.40  |
| ArceKV   | 0.3        | 198.747203 | 138.165683 | 336.91  |
| RocksDB  | 0.3        | 263.923579 | 323.140407 | 587.06  |
| ArceKV   | 0.4        | 251.521597 | 118.778167 | 370.30  |
| RocksDB  | 0.4        | 334.889127 | 178.803711 | 513.69  |
| ArceKV   | 0.5        | 296.937922 | 100.508206 | 397.45  |
| RocksDB  | 0.5        | 371.634243 | 112.806087 | 484.44  |
| ArceKV   | 0.6        | 343.646372 | 82.729359  | 426.38  |
| RocksDB  | 0.6        | 396.405045 | 91.314825  | 487.72  |
| ArceKV   | 0.7        | 371.43485  | 62.08856   | 433.52  |
| RocksDB  | 0.7        | 427.934935 | 67.941791  | 495.88  |
| ArceKV   | 0.8        | 395.596372 | 41.743701  | 437.34  |
| RocksDB  | 0.8        | 449.571008 | 44.896419  | 494.47  |
| ArceKV   | 0.9        | 416.868406 | 21.168589  | 438.04  |
| RocksDB  | 0.9        | 456.469977 | 22.728706  | 479.20  |