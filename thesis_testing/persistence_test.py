import os
import statistics
import subprocess
import time
from pathlib import Path

cur_dir = Path(".")
nvme_dev = "/dev/nvme1n1"

duckdb_file = cur_dir / "builds/duckdb_file"
duckdb_nvme = cur_dir / "builds/duckdb_nvme"

if not (duckdb_file.exists() and duckdb_nvme.exists()):
    print("Could not find the required build files.")
    exit()

sql_read = cur_dir / "sql/read.sql"
sql_write = cur_dir / "sql/write.sql"

if not (sql_read.exists() and sql_write.exists()):
    print("Could not find the required sql files.")
    exit()


def wrap(c, verbose):
    if verbose:
        return subprocess.run(c.split())
    else:
        return subprocess.run(
            c.split(),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )


def file_read_write_test(db_file, verbose):
    cw = wrap(f"{duckdb_file} -f {sql_write} {db_file}", verbose)
    cr = wrap(f"{duckdb_file} -f {sql_read} {db_file}", verbose)

    return cw.returncode & cr.returncode


def nvme_read_write_test(verbose):
    cw = wrap(f"{duckdb_nvme} -new -f {sql_write} {nvme_dev}", verbose)
    cr = wrap(f"{duckdb_nvme} -f {sql_read} {nvme_dev}", verbose)

    return cw.returncode & cr.returncode


class Tester:
    def __init__(self, timer, verbose):
        self._file_times = []
        self._nvme_times = []

        self._timer = timer
        self._verbose = verbose

    def get_result(self):
        if self._timer:
            print(
                f"\tFileTimer: {statistics.mean(self._file_times)}\n\tNvmeTimer: {statistics.mean(self._nvme_times)}"
            )

    def run_file(self, test_fun):
        tmp_db = (cur_dir / "test.db").absolute()

        if tmp_db.exists():
            os.remove(tmp_db)

        if self._timer:
            start_time = time.time()

        test_res = test_fun(tmp_db, self._verbose)

        if self._timer:
            end_time = time.time()
            self._file_times.insert(0, end_time - start_time)

        if test_res != 0:
            print("\t\tFAILED")

    def run_nvme(self, test_fun):
        if self._timer:
            start_time = time.time()

        test_res = test_fun(self._verbose)

        if self._timer:
            end_time = time.time()
            self._nvme_times.insert(0, end_time - start_time)

        if test_res != 0:
            print("\t\tFAILED")


if __name__ == "__main__":
    print("Running Persistence Tests...")
    tester = Tester(timer=True, verbose=True)

    print("\tStandard DuckDB, using files")
    for i in range(1):
        tester.run_file(file_read_write_test)

    print("\tModified DuckDB, using nvme")
    for i in range(1):
        tester.run_nvme(nvme_read_write_test)

    print("\nTest Finished.")
    tester.get_result()
