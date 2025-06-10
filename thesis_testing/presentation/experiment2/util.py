import statistics

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import seaborn as sns
from matplotlib.lines import Line2D
from scipy.stats import levene, ttest_ind

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


def exp2_ttest(
    data,
    baseline_impl="duckdb_standard",
    alpha=0.05,
    show=False,
    passthrough_test=False,
    show_p_values=False,
    show_error_bars=False,
    label=None,
):
    """
    Plots boxplots of runtimes grouped by scale factor and implementation,
    with asterisks showing significant one-tailed speedups/slowdowns vs. baseline.

    Parameters:
        data (dict): Nested dictionary with structure:
                     {scale_factor: {implementation: [runtimes...]}}
        baseline_impl (str): The key name for the baseline implementation
    """
    keys_to_keep = [10, 20, 40, 60, 80, 100, 200, 300]
    data = {k: v for k, v in data.items() if k in keys_to_keep}

    if not passthrough_test:
        for _, value in data.items():
            if "duckdb_xnvme_async_tqueue_no_passthrough" in value:
                del value["duckdb_xnvme_async_tqueue_no_passthrough"]

        labels = {
            "duckdb_standard": "Standard",
            "duckdb_xnvme_file": "File",
            "duckdb_xnvme_sync": "Sync",
            "duckdb_xnvme_async_squeue": "Async, Single Queue",
            "duckdb_xnvme_async_mqueue": "Async, Queue Pool",
            "duckdb_xnvme_async_tqueue": "Async, Thread Queues",
        }
    else:
        labels = {
            "duckdb_xnvme_async_tqueue": "Async, Thread Queues (Passthrough)",
            "duckdb_xnvme_async_tqueue_no_passthrough": "Async, Thread Queues (No Passthrough)",
        }
        data = {
            k: {key: value for key, value in v.items() if key in labels}
            for k, v in data.items()
        }
    # Transform into long-form DataFrame
    records = []
    for scale, impls in data.items():
        for impl, timings in impls.items():
            for t in timings:
                records.append(
                    {
                        "Scale Factor": scale,
                        "Implementation": labels[impl],
                        "Query Runtime (seconds)": t,
                    }
                )
    df = pd.DataFrame(records)

    # Sort scales for consistent plotting
    df["Scale Factor"] = pd.Categorical(
        df["Scale Factor"], categories=sorted(data.keys(), key=int), ordered=True
    )

    # Compute one-tailed p-values
    if show_p_values:
        significance = {}
        for scale in data:
            base = data[scale][baseline_impl]
            for impl in data[scale]:
                if impl == baseline_impl:
                    continue
                other = data[scale][impl]

                _, p = levene(base, other)
                if p > 0.05:
                    var_equal = True
                else:
                    var_equal = False

                t_stat, p_two_tailed = ttest_ind(base, other, equal_var=var_equal)

                mean_base = pd.Series(base).mean()
                mean_impl = pd.Series(other).mean()

                if mean_impl < mean_base:
                    # Test for significant speedup
                    p_one_tailed = p_two_tailed / 2 if t_stat > 0 else 1.0
                    significance[(scale, impl)] = "**" if p_one_tailed < alpha else ""
                else:
                    # Test for significant slowdown
                    p_one_tailed = p_two_tailed / 2 if t_stat < 0 else 1.0
                    significance[(scale, impl)] = "*" if p_one_tailed < alpha else ""

    # Plot
    plt.figure(figsize=(20, 10))
    plt.subplots_adjust(bottom=0.2, right=0.95, left=0.05)
    if show_error_bars:
        sns.barplot(
            data=df,
            x="Scale Factor",
            y="Query Runtime (seconds)",
            hue="Implementation",
            errorbar="se",
        )
    else:
        sns.barplot(
            data=df,
            x="Scale Factor",
            y="Query Runtime (seconds)",
            hue="Implementation",
            errorbar=None,
        )
    plt.title("Query Runtime per Implementation and Scale Factor")

    # Create legend entries for both implementation and significance markers
    handles, labels_text = plt.gca().get_legend_handles_labels()

    # Add custom legend entries for significance and error bars
    custom_handles = [
        Line2D(
            [0],
            [0],
            marker="",
            color="white",
            markerfacecolor="black",
            markersize=15,
            label="|  = Standard error",
        ),
        Line2D(
            [0],
            [0],
            marker="",
            color="w",
            markerfacecolor="black",
            markersize=10,
            label=f"*  = Significant slowdown (p>{1-alpha:.2f})",
        ),
        Line2D(
            [0],
            [0],
            marker="",
            color="w",
            markerfacecolor="black",
            markersize=10,
            label=f"** = Significant speedup (p<{alpha:.2f})",
        ),
    ]

    # Combine all legend entries
    label_text = [
        "|   = Standard error of the mean",
        f"*  = Significant slowdown (p>{1-alpha:.2f})",
        f"** = Significant speedup (p<{alpha:.2f})",
    ]
    if not show_p_values:
        custom_handles = [custom_handles[0]]
        label_text = [label_text[0]]
    if not show_error_bars:
        custom_handles = custom_handles[1:] if len(custom_handles) > 1 else []
        label_text = label_text[1:] if len(label_text) > 1 else []

    all_handles = handles + custom_handles
    all_labels = labels_text + label_text
    # Create the combined legend
    plt.legend(
        handles=all_handles,
        labels=all_labels,
        title="",
        loc="upper left",
        fontsize=12,
    )

    y_max = df["Query Runtime (seconds)"].max() + 0.05  # 5% padding

    if show_p_values:
        for (scale, impl), mark in significance.items():
            if mark:
                # Get the position of the bar
                scale_idx = list(sorted(data.keys(), key=int)).index(scale)
                impl_idx = list(labels.keys()).index(impl)

                # Get the height of the bar
                group = df[
                    (df["Scale Factor"] == scale)
                    & (df["Implementation"] == labels[impl])
                ]
                bar_height = group["Query Runtime (seconds)"].mean()

                # Calculate x-position based on hue ordering in seaborn
                hue_offset = len(labels) / 2  # Number of bars in each group
                width = 0.8 / len(labels)  # Width of each bar
                x_pos = scale_idx + (impl_idx - hue_offset + 0.5) * width

                # Place text above the bar
                plt.text(
                    x_pos,
                    bar_height + y_max * 0.005,
                    mark,
                    ha="center",
                    fontsize=12,
                    fontweight="bold",
                )

    plt.ylim(0, y_max)

    plt.tight_layout()
    if show:
        plt.show()
    else:
        if label:
            plt.savefig(f"{label}.png")
