import statistics

import matplotlib.pyplot as plt
import numpy as np


def exp2_plot(title, results, show=False):
    labels = list(results.keys())
    x = np.arange(len(labels))

    y1 = [statistics.mean(rd["duckdb_xnvme_async_tqueue"]) for rd in results.values()]
    y2 = [
        statistics.mean(rd["duckdb_xnvme_async_tqueue_no_passthrough"])
        for rd in results.values()
    ]

    # plot
    width = 0.35
    fix, ax = plt.subplots(figsize=(12, 6))
    _ = ax.bar(x - width / 2, y1, width, label="NVMe no passthrough")
    _ = ax.bar(x + width / 2, y2, width, label="NVMe with passthrough")

    # labels and formatting
    ax.set_xlabel("Scale Factor")
    ax.set_ylabel("Seconds")
    ax.set_title(title)
    ax.set_xticks(x)
    ax.set_xticklabels(labels)
    ax.legend(loc="upper left")

    # produce plot
    plt.tight_layout()
    if show:
        plt.show()
    else:
        plt.savefig("experiment1/plot")
