from pathlib import Path

cur_dir = Path(".")

duckdb_file = cur_dir / "builds/duckdb_file"
duckdb_nvme = cur_dir / "builds/duckdb_nvme"

if not (duckdb_file.exists() and duckdb_nvme.exists()):
    print("Could not find the required build files.")
    exit()
