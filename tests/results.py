#!/usr/bin/env python3

import matplotlib.pyplot as plt


LIBRUNTIME = [
    350000,
    675000,
    900000,
    1050000,
    1100000,
    1130000,
    1150000,
    1140000,
]
AFLPP = [
    90000,
    170000,
    220000,
    265000,
    270000,
    270000,
    280000,
    280000,
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
