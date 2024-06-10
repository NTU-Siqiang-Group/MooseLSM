## MooseLSM: a flexible LSM key-value store

MooseLSM is developed based on [RocksDB](https://github.com/facebook/rocksdb), a prevalent LSM-tree key-value store.
To make the structure of LSM tree more flexible and consequently more powerful, MooseLSM removes the constraint of 
a fix size ratio between consecutive levels and allows users to set an arbitrary number for each level as its capacity (i.e., `options.level_capacities`).
In addition, MooseLSM can accommodate multiple sorted run at each level and users can set an arbitrary number for the number of run as well (i.e., `options.run_numbers`).
By doing the relaxation above, users are able to implement various kinds of academic compaction policies (e.g., Tiering, Dostoevsky, Wacky, etc.).

In addition, we also propose a decent method for you to achieve an optimal tradeoff in our [paper](https://siqiangluo.com/docs/SIGMOD24_MOOSE_Camera___Junfeng___Fan.pdf).
Here is the bibtex for you to refer
```
@article{liu2024structural,
  title={Structural Designs Meet Optimality: Exploring Optimized LSM-tree Structures in A Colossal Configuration Space},
  author={Liu, Junfeng and Wang, Fan and Mo, Dingheng and Luo, Siqiang},
  journal={Proceedings of the ACM on Management of Data},
  volume={2},
  number={3},
  pages={1--26},
  year={2024},
  publisher={ACM New York, NY, USA}
}
```

## build
Dependencies:
- gflags
- cmake
- gcc/g++

To build the library of MooseLSM, just run:
```
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make install
```

To embed the key-value store in your c++ application:
```c++
#include "rocksdb/db.h"
#include "rocksdb/options.h"

...
rocksdb::Options options;
options.level_capacities = {...}; // a vector of capacities
options.run_numbers = {...}; // a vector of run numbers
auto status = rocksdb::Open(options, "<path-to-your-db>", &db);
if (!status.ok()) {
  // fail to open the db
  exit(1);
}
// do the workload with db
db->close();
delete db;
```

## License
Moose is licensed under [NTUITIVE PTE LTD Dual License](LICENSE).
