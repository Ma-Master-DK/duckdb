import re
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
from experiment1.util import *
from experiment2.util import *

cur_dir = Path(".")
show = False

plt.rcParams["font.size"] = 15


def parse_results(data_file):
    results = {}

    cur_sf = 0
    cur_ex = {}

    with open(data_file, "r") as f:
        lines = f.readlines()

    for line in lines:
        if match := re.search(r"sf: ((\d+\.)?\d+)", line):
            # new scale factor
            if cur_sf > 0:
                results[cur_sf] = cur_ex

            cur_sf = float(match.group(1))
            cur_ex = {}

        elif match := re.search(r"duckdb*", line):
            # test results
            test_name, *test_times = line.split()
            cur_ex[test_name] = list(map(float, test_times))

        else:
            # undefined
            continue

    # add latest scale factor at EOF
    results[cur_sf] = cur_ex

    return results


if __name__ == "__main__":
    print("Reading Results..")
    results = parse_results(cur_dir / "../results/test_results.txt")

    print("\nPresenting Experiment 1..")
    # exp1_plot("Read Latency", results)
    exp2_ttest(
        results,
        baseline_impl="duckdb_xnvme_async_tqueue_no_passthrough",
        alpha=0.05,
        passthrough_test=True,
        label="Experiment1",
    )
    exp2_ttest(
        results,
        baseline_impl="duckdb_xnvme_async_tqueue_no_passthrough",
        alpha=0.05,
        passthrough_test=True,
        show_p_values=True,
        label="Experiment1-t-test",
    )
    exp2_ttest(
        results,
        baseline_impl="duckdb_xnvme_async_tqueue_no_passthrough",
        alpha=0.05,
        passthrough_test=True,
        show_p_values=True,
        show_error_bars=True,
        label="Experiment1-t-test-error-bars",
    )
    exp1_table(results)

    print("\nPresenting Experiment 2..")
    # exp2_plot("Read Latency", results)
    exp2_table(results)

    print("Running Experiment 2 T-Test..")
    exp2_ttest(results, alpha=0.05, label="Experiment2")
    exp2_ttest(results, alpha=0.05, show_p_values=True, label="Experiment2-t-test")
    exp2_ttest(
        results,
        alpha=0.05,
        show_p_values=True,
        show_error_bars=True,
        label="Experiment2-t-test-error-bars",
    )

    print("Done!")
