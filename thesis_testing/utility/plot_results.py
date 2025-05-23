import os
import re
import subprocess
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

# general use
test_dir = Path("..")
verbose = False

r_file = test_dir / "results/test_results.txt"


def parse_results(data_file):
    res = {}
    cur_sf = 0
    cur_rs = []

    with open(data_file, "r") as f:
        lines = f.readlines()

    for line in lines:
        if match := re.search(r"sf: ((\d+\.)?\d+)", line):
            # new scale factor
            if cur_sf > 0:
                res[cur_sf] = cur_rs

            cur_sf = float(match.group(1))
            cur_rs = []

        elif match := re.search(r"Average time.*: (\d*\.\d+).*", line):
            # extract time
            cur_rs.append(float(match.group(1)))

        else:
            continue

    # add latest scale factor at EOF
    res[cur_sf] = cur_rs

    write_results = {k: v[:2] for k, v in res.items()}
    read_results = {k: v[2:] for k, v in res.items()}

    return read_results, write_results


def plot_results(title, results):
    labels = list(results.keys())
    y1, y2 = map(list, zip(*results.values()))

    x = np.arange(len(labels))
    width = 0.35

    # plot
    fix, ax = plt.subplots()
    _ = ax.bar(x - width / 2, y1, width, label="file")
    _ = ax.bar(x + width / 2, y2, width, label="nvme")

    # labels and formatting
    ax.set_xlabel("Scale Factor")
    ax.set_ylabel("Time")
    ax.set_title(title)
    ax.set_xticks(x)
    ax.set_xticklabels(labels)
    ax.legend()

    # produce plot
    plt.tight_layout()
    plt.savefig(test_dir / f"results/{title}")
    if verbose:
        plt.show()


if __name__ == "__main__":
    read_results, write_results = parse_results(r_file)

    plot_results("Read", read_results)
    plot_results("Write", write_results)
