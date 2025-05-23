#!/usr/bin/env bash

# Configuration
DB="benchmark.duckdb"
DEV="/dev/nvme1n1"
CUSTOM="../builds/duckdb_nvme"
STANDARD="../builds/duckdb_file"
sfs=(0.01 0.1 1)
RUNS=5

if ! sudo -v; then
        echo "Error: sudo required to clear OS caches."
        exit 1
fi

function remove_existing_db() {
        if [ -f "$DB" ]; then
                echo "Removing previous database..."
                rm "$DB"
        fi
}

function clear_caches() {
        echo "Clearing system cache..."
        sudo sync
        echo 3 | sudo tee /proc/sys/vm/drop_caches > /dev/null
}

function run_write_benchmark() {
        local bin=$1
        local label=$2
        local sf=$3
        local file=$4
        local total_s=0
        local query="EXPLAIN ANALYZE CALL dbgen(sf=$sf);"

        echo "-----------------------------------"
        echo "Benchmark $label"
        for i in $(seq 1 $RUNS); do
                clear_caches
                echo "Running query..."
                result=$(echo "$query" | $bin $file)
                time_s=$(echo "$result" | grep "Total Time" | sed -E 's/[^0-9.]//g')
                echo "  Run $i: $time_s s"
                total_s=$(echo "$total_s + $time_s" | bc)
        done

        avg=$(echo "scale=3; $total_s / $RUNS" | bc)
        echo "Average time for $label: $avg s"
        echo
}

function run_read_benchmark() {
        local bin=$1
        local label=$2
        local file=$3
        local total_s=0
        local query="EXPLAIN ANALYZE SELECT * FROM customer;"

        echo "-----------------------------------"
        echo "Benchmark $label"
        for i in $(seq 1 $RUNS); do
                clear_caches
                echo "Running query..."
                result=$(echo "$query" | $bin $file)
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

remove_existing_db

for sf in "${sfs[@]}"; do
        echo "sf: $sf"
        echo "===> WRITE-INTENSIVE QUERY BENCHMARK! RUNS: $RUNS"
        run_write_benchmark "$STANDARD" "Standard DuckDB (Write)" "$sf" "$DB"
        run_write_benchmark "$CUSTOM -new" "xnvme DuckDB (Write) " "$sf" "$DEV"


        echo "===> READ-INTENSIVE QUERY BENCHMARK! RUNS: $RUNS"
        run_read_benchmark "$STANDARD" "Standard DuckDB (Read)" "$DB"
        run_read_benchmark "$CUSTOM" "xnvme DuckDB (Read)" "$DEV"

        echo ""
done
