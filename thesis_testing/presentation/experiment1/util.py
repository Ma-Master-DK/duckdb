import statistics

import matplotlib.pyplot as plt
import numpy as np

sf_threshold = 1.0


def clean_results(results):
    return {k: v for k, v in results.items() if float(k) >= sf_threshold}


def exp1_plot(title, results, show=False):
    results = clean_results(results)

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
    _ = ax.bar(x - width / 2, y1, width, label="NVMe passthrough")
    _ = ax.bar(x + width / 2, y2, width, label="NVMe no passthrough")

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
        plt.savefig("experiment1/plot")


def exp1_table(results):
    results = clean_results(results)

    rows = results.keys()
    cols = [
        "duckdb_xnvme_async_tqueue",
        "duckdb_xnvme_async_tqueue_no_passthrough",
    ]

    res = "Scale Factor"
    for c in cols:
        res += f",{c}"

    for r in rows:
        res += f"\n{r}"
        for c in cols:
            res += f",{round(statistics.mean(results[r][c]), 3)}"

    print("\tSaving Table to file.")
    with open("experiment1/table.csv", "w") as f:
        f.write(res)
