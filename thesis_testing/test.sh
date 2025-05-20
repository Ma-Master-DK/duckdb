#!/usr/bin/env bash

# Configuration
DB="benchmark.duckdb"
DEV="/dev/nvme1n1"
CUSTOM="builds/duckdb_nvme"
STANDARD="builds/duckdb_file"
SQL_CREATE="./create_table.sql"
SQL_WRITE="./io_write.sql"
SQL_READ="./io_read.sql"
RUNS=2

if ! sudo -v; then
        echo "Error: sudo required to clear OS caches."
        exit 1
fi

if [ -f "$DB" ]; then
        echo "Removing previous database..."
        rm "$DB"
fi

echo "Creating benchmark databases..."
$CUSTOM $DEV "-new" -f $SQL_CREATE 2>/dev/null
$STANDARD $DB -f $SQL_CREATE 2>/dev/null

function clear_caches() {
        echo "Clearing system cache..."
        sudo sync
        echo 3 | sudo tee /proc/sys/vm/drop_caches > /dev/null
}

function run_benchmark() {
        local bin=$1
        local label=$2
        local query=$3
        local file=$4
        local total_s=0

        echo "-----------------------------------"
        echo "Benchmark $label"
        for i in $(seq 1 $RUNS); do
                clear_caches
                echo "Running query..."
                result=$($bin $file -f $query)
                time_s=$(echo "$result" | grep "Total Time" | sed -E 's/[^0-9.]//g')
                echo "  Run $i: $time_s s"
                total_s=$(echo "$total_s + $time_s" | bc)
        done

        avg=$(echo "scale=3; $total_s / $RUNS" | bc)
        echo "Average time for $label: $avg s"
        echo
}

#-------------------------------------------
# RUN BENCHMARKS
# ------------------------------------------


echo "===> WRITE-INTENSIVE QUERY BENCHMARK"
run_benchmark "$CUSTOM" "xnvme DuckDB (Write)" "$SQL_WRITE" "$DEV"
run_benchmark "$STANDARD" "Standard DuckDB (Write)" "$SQL_WRITE" "$DB"


echo "===> READ-INTENSIVE QUERY BENCHMARK"
run_benchmark "$CUSTOM" "xnvme DuckDB (Read)" "$SQL_READ" "$DEV"
run_benchmark "$STANDARD" "Standard DuckDB (Read)" "$SQL_READ" "$DB"

