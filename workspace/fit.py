import re

num_re = re.compile(r'\d+')

x = []
y = []

with open('log/leveling_noblock.log', 'r') as f:
  for line in f:
    if 'COMPACTION_END' in line:
      nums = num_re.findall(line)
      us = int(nums[-1])
      output = int(nums[-2])
      input_n = int(nums[-3])
      x.append(input_n + output)
      y.append(us)

import matplotlib.pyplot as plt
import numpy as np

x = np.array(x)
y = np.array(y)

coef = np.polyfit(x, y, 1)
slope, intercept = coef

def linear_function(x):
  return slope * x + intercept

print(slope, intercept)

x_fit = np.linspace(min(x), max(x), 100)
y_fit = linear_function(x_fit)
plt.scatter(x, y, color='blue', label='Compaction Points')
plt.plot(x_fit, y_fit, color='red', label='Fitted line')
plt.xlabel('compaction size')
plt.ylabel('Time (us)')
plt.legend()
plt.savefig('result/compaction_time.png')