## MooseLSM: a flexible LSM key-value store

MooseLSM is developed based on [RocksDB](https://github.com/facebook/rocksdb), a prevalent LSM-tree key-value store.
To make the structure of LSM tree more flexible and consequently more powerful, MooseLSM removes the constraint of 
a fix size ratio between consecutive levels and allows users to set an arbitrary number for each level as its capacity (i.e., `options.level_capacities`).
In addition, MooseLSM can accommodate multiple sorted run at each level and users can set an arbitrary number for the number of run as well (i.e., `options.run_numbers`).
By doing the relaxation above, users are able to implement various kinds of academic compaction policies (e.g., Tiering, Dostoevsky, Wacky, etc.).

In addition, we also propose a decent method for you to achieve an optimal structure in our [paper](https://siqiangluo.com/docs/SIGMOD24_MOOSE_Camera___Junfeng___Fan.pdf).
Here is the bibtex for you to refer
```

```

## License
Moose is licensed under [NTUITIVE PTE LTD Dual License](LICENSE).
