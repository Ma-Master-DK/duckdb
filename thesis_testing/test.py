import os
import statistics
import subprocess
import time
from pathlib import Path

# general use
cur_dir = Path(".")
nvme_dev = "/dev/nvme1n1"
test_amount = 10
verbose = True

# these are the builds of DuckDB we need for testing
duckdb_file = cur_dir / "builds/duckdb_file"
duckdb_nvme = cur_dir / "builds/duckdb_nvme"
if not (duckdb_file.exists() and duckdb_nvme.exists()):
    print("Could not find the required build files.")
    exit()

# sql files, THIS MIGHT NEED REWORKING
sql_write = cur_dir / "sql/write.sql"
sql_read = cur_dir / "sql/read.sql"
if not (sql_write.exists() and sql_read.exists()):
    print("Could not find sql files.")
    exit()


class Tester:
    def __init__(self):
        """
        Initialize 'Tester' Class.

        We use file_times and nvme_times to store timers for each
        build, respectively
        """
        self._file_times = []
        self._nvme_times = []

    def get_result(self):
        """
        Get the result from the internal timers.
        """
        print(
            f"\tFileTimer: {statistics.mean(self._file_times)}\n\tNvmeTimer: {statistics.mean(self._nvme_times)}"
        )

    def run_file(self, sql, new_db):
        """
        Runs the given SQL against the file build of DuckDB.
        """

        # temporary db file
        tmp_db = (cur_dir / "test.db").absolute()
        if new_db and tmp_db.exists():
            os.remove(tmp_db)

        # start a DuckDB session against the specified db
        # runs in a child process
        proc = subprocess.Popen(
            [duckdb_file, tmp_db],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        start_time = time.perf_counter()

        # execute SQL against db
        # adds an 'exit' statement for DuckDB to exit session after SQL
        sql_cmd = sql + "\n.exit"
        proc.stdin.write(sql_cmd.encode("utf-8"))
        proc.stdin.flush()

        end_time = time.perf_counter()
        elapsed = end_time - start_time
        self._file_times.insert(0, elapsed)

        # wait for child process to stop
        stdout, stderr = proc.communicate()

        if stderr:
            print("\t\tFAILED")

        if verbose:
            print(
                stdout.decode("utf-8").strip(),
                stderr.decode("utf-8").strip(),
            )

    def run_nvme(self, sql, new_db):
        """
        Runs the given SQL against the nvme build of DuckDB
        """

        # start a DuckDB session against the nvme db
        # runs in a child process
        proc = subprocess.Popen(
            [duckdb_nvme, new_db, nvme_dev],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        start_time = time.perf_counter()

        # execute SQL against db
        # adds an 'exit' statement for DuckDB to exit session after SQL
        sql_cmd = sql + "\n.exit"
        proc.stdin.write(sql_cmd.encode("utf-8"))
        proc.stdin.flush()

        end_time = time.perf_counter()
        elapsed = end_time - start_time
        self._nvme_times.insert(0, elapsed)

        # wait for child process to stop
        stdout, stderr = proc.communicate()

        if stderr:
            print("\t\tFAILED")

        if verbose:
            print(
                stdout.decode("utf-8").strip(),
                stderr.decode("utf-8").strip(),
            )


if __name__ == "__main__":
    print("Running Persistence Tests...")
    tester = Tester()

    with open(sql_write) as f:
        sw = f.read()

    with open(sql_read) as f:
        sr = f.read()

    print("\tStandard DuckDB, using files")
    for i in range(test_amount):
        tester.run_file(sw, new_db=True)
        tester.run_file(sr, new_db=False)

    print("\tModified DuckDB, using nvme")
    for i in range(test_amount):
        tester.run_nvme(sw, new_db="-new")
        tester.run_nvme(sr, new_db="")

    print("\nTest Finished.")
    tester.get_result()
