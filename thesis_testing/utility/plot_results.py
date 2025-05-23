import os
import re
import subprocess
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

# general use
test_dir = Path("..")
verbose = False
dir = "vertical"  # horizontal/vertical

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

        elif match := re.search(r"\d*\.\d+", line):
            cur_rs.append(float(line))

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


def latex_results_horizontal(title, data):
    cols = 1
    row_header = r"\textbf{Scale Factor}"
    row_file = "File"
    row_nvme = "Nvme"
    row_gain = "Gain"

    for sf, (file, nvme) in data.items():
        cols += 1
        row_header += f" & {sf}"
        row_file += f" & {file}"
        row_nvme += f" & {nvme}"
        row_gain += f" & {round(file/nvme, 2)}"

    cols = "c" + ("|c" * (cols - 1))

    return f"""
\\begin{{table}}[H]
    \\centering
    \\begin{{tabular}}{{{cols}}}
        {row_header} \\\\\\hline
        {row_file} \\\\
        {row_nvme} \\\\\\hline
        {row_gain} \\\\
    \\end{{tabular}}
    \\caption{{{title}}}
    \\label{{}}
\\end{{table}}
"""


def latex_results_vertical(title, data):
    cols = "c|cc|c"

    rows = """        \\textbf{{Scale Factor}} & File & Nvme & Gain \\\\\\hline"""

    for sf, (file, nvme) in data.items():
        rows += f"""
        {sf} & {file} & {nvme} & {round(file/nvme, 2)} \\\\"""

    return f"""
\\begin{{table}}[H]
    \\centering
    \\begin{{tabular}}{{{cols}}}
{rows}
    \\end{{tabular}}
    \\caption{{{title}}}
    \\label{{}}
\\end{{table}}
"""


if __name__ == "__main__":
    read_results, write_results = parse_results(r_file)

    plot_results("Read", read_results)
    plot_results("Write", write_results)

    if dir == "horizontal":
        print(latex_results_horizontal("Read", read_results))
        print(latex_results_horizontal("Write", write_results))
    else:
        print(latex_results_vertical("Read", read_results))
        print(latex_results_vertical("Write", write_results))
