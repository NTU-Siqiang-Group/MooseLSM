methods = ['dynamic', 'leveling']

props = [f"{0.1 * i:.1f}" for i in range(1, 10)]

results = [['method', 'read_ratio', 'reads', 'writes', 'total']]

for p in props:
  for m in methods:
    filename = f'{p}_{m}.log'
    ret = [m, p]
    with open(filename, 'r') as f:
      for line in f:
        if 'rocksdb.db.get.micros' in line:
          t = int(line.split(' ')[-1])
          ts = t / 1e6
          ret.append(ts)
        elif 'rocksdb.db.write.micros' in line:
          t = int(line.split(' ')[-1])
          ts = t / 1e6
          ret.append(ts)
    ret.append(f'{ret[2] + ret[3]:.2f}')
    results.append(ret)

with open('tmp.csv', 'w+') as f:
  for l in results:
    sl = [str(i) for i in l]
    f.write(','.join(sl) + '\n')
