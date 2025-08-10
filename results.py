#!/usr/bin/env python3

import matplotlib.pyplot as plt


LIBRUNTIME = [
    340000,
    640000,
    840000,
    990000,
    1050000,
    1050000,
    1060000,
    1100000,
]
AFLPP = [
    55000,
    100000,
    135000,
    155000,
    170000,
    170000,
    170000,
    185000,
]
x = list(range(1, len(LIBRUNTIME) + 1))

fig, ax = plt.subplots()

ax.plot(x, LIBRUNTIME, label="cheetah")
ax.plot(x, AFLPP, label="AFL++")
ax.set_ylim(bottom=0)

plt.title("Persistent Mode Performance")
plt.xlabel("#cores")
plt.ylabel("exec/s")
plt.xticks(x)
plt.legend(loc="upper left")
plt.grid()
plt.show()
