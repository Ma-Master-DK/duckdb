import statistics

import matplotlib.pyplot as plt
import numpy as np

sf_threshold = 10.0


def clean_results(results):
    return {k: v for k, v in results.items() if float(k) >= sf_threshold}


def exp2_plot(title, results, show=False):
    results = clean_results(results)

    labels = list(results.keys())
    x = np.arange(len(labels))

    y1 = [statistics.mean(rd["duckdb_standard"]) for rd in results.values()]
    y2 = [statistics.mean(rd["duckdb_xnvme_file"]) for rd in results.values()]
    y3 = [statistics.mean(rd["duckdb_xnvme_sync"]) for rd in results.values()]
    y4 = [statistics.mean(rd["duckdb_xnvme_async_squeue"]) for rd in results.values()]
    y5 = [statistics.mean(rd["duckdb_xnvme_async_mqueue"]) for rd in results.values()]
    y6 = [statistics.mean(rd["duckdb_xnvme_async_tqueue"]) for rd in results.values()]

    # plot
    width = 0.15
    fix, ax = plt.subplots(figsize=(12, 6))
    _ = ax.bar(x - (width / 2 + width * 2), y1, width, label="Standard")
    _ = ax.bar(x - (width / 2 + width), y2, width, label="xNVMe File")
    _ = ax.bar(x - (width / 2), y3, width, label="xNVMe Sync")
    _ = ax.bar(x + (width / 2), y4, width, label="xNVMe Async, Single Queue")
    _ = ax.bar(x + (width / 2 + width), y5, width, label="xNVMe Async, Queue Pool")
    _ = ax.bar(
        x + (width / 2 + width * 2), y6, width, label="xNVMe Async, Thread Queues"
    )

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
        print("\tShowing Plot.")
        plt.show()
    else:
        print("\tSaving Plot as file.")
        plt.savefig("experiment2/plot")


def exp2_table(results):
    results = clean_results(results)

    rows = results.keys()
    cols = [
        "duckdb_standard",
        "duckdb_xnvme_file",
        "duckdb_xnvme_sync",
        "duckdb_xnvme_async_squeue",
        "duckdb_xnvme_async_mqueue",
        "duckdb_xnvme_async_tqueue",
    ]

    res = "Scale Factor"
    for c in cols:
        res += f",{c}"

    for r in rows:
        res += f"\n{r}"
        for c in cols:
            res += f",{round(statistics.mean(results[r][c]), 3)}"

    print("\tSaving Table to file.")
    with open("experiment2/table.csv", "w") as f:
        f.write(res)
