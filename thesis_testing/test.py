import os
import re
import statistics
import subprocess
import time
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

# general use
cur_dir = Path(".")
verbose = False

# db configuration
file_db = cur_dir / "test.db"
nvme_db = "/dev/nvme1n1"

# test meta information
test_amount = 3
test_scales = [0.01, 0.1, 1]

# these are the builds of DuckDB we need for testing
duckdb_file = cur_dir / "builds/duckdb_file"
duckdb_nvme = cur_dir / "builds/duckdb_nvme"
if not (duckdb_file.exists() and duckdb_nvme.exists()):
    print("Could not find the required build files.")
    exit()

# sql file
sql_read = cur_dir / "sql/read.sql"
if not (sql_read.exists()):
    print("Could not find sql file.")
    exit()


class Tester:
    def __init__(self, build, db, scale_factor):
        """
        Initialize 'Tester' Class.

        We use file_times and nvme_times to store timers for each
        build, respectively
        """

        # configuration of DuckDB
        self._build = build
        self._db = db

        # scale factor for generating test data
        self._scale_factor = scale_factor

        # time logs for read/write
        self._read_times = []
        self._write_times = []

    def get_result(self):
        """
        Get the result from the internal timers.
        """

        # print("read:  ", self._read_times)
        # print("write: ", self._write_times)

        read = statistics.mean(self._read_times)
        write = statistics.mean(self._write_times)

        print(f"\t\t\tRead:  {read: .9f}\n\t\t\tWrite: {write: .9f}")

        return read, write

    def test_write(self, sql, new_db=""):
        """
        Runs the given SQL against the specified build of DuckDB
        """

        # start a DuckDB session against the db
        # runs in a child process
        proc = subprocess.Popen(
            [self._build, new_db, self._db],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        sql_cmd = f"explain analyse call dbgen(sf={self._scale_factor});\n.exit\n"
        stdout, stderr = proc.communicate(sql_cmd.encode("utf-8"))

        match = re.search(r"Total Time:\s*([\d.]+)s", stdout.decode("utf-8"))
        if match:
            self._write_times.insert(0, float(match.group(1)))
        else:
            print("no write time.")

        if stderr:
            print("\t\tFAILED")

        if verbose:
            print(
                stdout.decode("utf-8").strip(),
                stderr.decode("utf-8").strip(),
            )

    def test_read(self, sql):
        """
        Runs the given SQL against the specified build of DuckDB
        """

        # start a DuckDB session against the db
        # runs in a child process
        proc = subprocess.Popen(
            [self._build, self._db],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        sql_cmd = f"{sql}\n.exit\n"
        stdout, stderr = proc.communicate(sql_cmd.encode("utf-8"))

        match = re.search(r"Total Time:\s*([\d.]+)s", stdout.decode("utf-8"))
        if match:
            self._read_times.insert(0, float(match.group(1)))
        else:
            print("no read time.")

        if stderr:
            print("\t\tFAILED")

        if verbose:
            print(
                stdout.decode("utf-8").strip(),
                stderr.decode("utf-8").strip(),
            )


def run_tests():
    read_results = {}
    write_results = {}

    # load sql to execute against db
    with open(sql_read) as f:
        sql = f.read()

    print("Running Tests..")

    for scale_factor in test_scales:
        read_results[scale_factor] = []
        write_results[scale_factor] = []

        print(f"\n\tTesting for scale factor: {scale_factor}")

        # test for standard DuckDB
        print("\t\tStandard DuckDB, file")
        file_tester = Tester(duckdb_file, file_db, scale_factor)
        for i in range(test_amount):
            if file_db.exists():
                os.remove(file_db)

            file_tester.test_write(sql)
            file_tester.test_read(sql)

        read, write = file_tester.get_result()
        read_results[scale_factor].append(read)
        write_results[scale_factor].append(write)

        # test for modified DuckDB, nvme
        print("\t\tModified DuckDB, nvme")
        nvme_tester = Tester(duckdb_nvme, nvme_db, scale_factor)
        for i in range(test_amount):
            nvme_tester.test_write(sql, "-new")
            nvme_tester.test_read(sql)

        read, write = nvme_tester.get_result()
        read_results[scale_factor].append(read)
        write_results[scale_factor].append(write)

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
    plt.savefig(cur_dir / f"plots/{title}")
    if verbose:
        plt.show()


if __name__ == "__main__":
    read_results, write_results = run_tests()

    plot_results("Read", read_results)
    plot_results("Write", write_results)
