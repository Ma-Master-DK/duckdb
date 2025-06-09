#!/usr/bin/env bash

# Configuration
DB="benchmark.duckdb"
DEV="/dev/nvme1n1"
CHAR_DEV="/dev/ng1n1"
STANDARD="../builds/duckdb_standard"
NVME_FILE="../build/duckdb_xnvme_file"
NVME_SYNC="../build/duckdb_xnvme_sync"
NVME_ASYNC_SQUEUE="../build/duckdb_xnvme_async_squeue"
NVME_ASYNC_MQUEUE="../build/duckdb_xnvme_async_mqueue"
NVME_ASYNC_TQUEUE="../build/duckdb_xnvme_async_tqueue"
NVME_ASYNC_TQUEUE_NO_PASSTHROUGH="../build/duckdb_xnvme_async_tqueue_no_passthrough"

sfs=(0.01 0.1 1 2 4 6 8 10 20 40 60 80 100 200 300)
RUNS=100
RESULTS="../results/test_results.txt"

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
        sudo sync
        echo 3 | sudo tee /proc/sys/vm/drop_caches > /dev/null
}

function run_write_benchmark() {
        local bin=$1
        local label=$2
        local sf=$3
        local file=$4
        local total_s=0
        local steps=10
        local query=""

        for ((step=0; step<steps; step++)); do
                query+="CALL dbgen(sf=$sf, children=$steps, step=$step); "
        done

        echo "-----------------------------------"
        echo "Benchmark $label"
        if [ "$file" = "$DB" ]; then
                remove_existing_db
        fi
        clear_caches
        echo -e "\tRunning query..."
        result=$(echo "$query" | $bin $file)
        time_s=$(echo "$result" | grep "Total Time" | sed -E 's/[^0-9.]//g')
        echo -e "\t\tRun 1: $time_s s"
        total_s=$(echo "$total_s + $time_s" | bc)

        avg=$(echo "scale=3; $total_s / 1" | bc)
        echo -e "\nAverage time for $label: $avg s"
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
        echo -n "$label" >> $RESULTS
        for i in $(seq 1 $RUNS); do
                echo -n " " >> $RESULTS
                clear_caches
                result=$(echo "$query" | $bin $file)
                time_s=$(echo "$result" | grep "Total Time" | sed -E 's/[^0-9.]//g')
                echo -n "$time_s" >> $RESULTS
                total_s=$(echo "$total_s + $time_s" | bc)
        done

        avg=$(echo "scale=3; $total_s / $RUNS" | bc)
        echo -e "\nAverage time for $label: $avg s"
        echo "" >> $RESULTS
        echo
}

function run_tpch_query() {
        local bin=$1
        local label=$2
        local query_num=$3
        local sf=$4
        local file=$5
        local tpch="tpch-$query_num.txt"
        local tpch_answer="tpch-answer-$query_num.txt"

        echo "-----------------------------------"
        echo "Testing $label"
        local tpch_query="PRAGMA tpch($query_num);"
        local tpch_answer_query="FROM tpch_answers() WHERE query_nr=$query_num AND scale_factor=$sf;"

        echo "$tpch_query" | $bin $file > "$tpch"
        echo "$tpch_answer_query" | $bin $file > "$tpch_answer"

        python3 check_tpch.py "$tpch" "$tpch_answer"

        STATUS=$?

        if [ $STATUS -eq 0 ]; then
                echo -e "\t✅ Output matches expected output"
        elif [ $STATUS -eq 2 ]; then
                echo -e "\t❌ Output does not match expected output"
        else
                echo -e "\t⚠ Error running the checker"
        fi

        rm "$tpch"
        rm "$tpch_answer"
}

#-------------------------------------------
# RUN BENCHMARKS
# ------------------------------------------

remove_existing_db

echo "" > $RESULTS

for sf in "${sfs[@]}"; do
        echo "sf: $sf"
        echo "sf: $sf" >> $RESULTS
        echo "===> WRITE-INTENSIVE QUERY BENCHMARK! RUNS: $RUNS"
        run_write_benchmark "$STANDARD" "Standard DuckDB (Write)" "$sf" "$DB"
        run_write_benchmark "$NVME_ASYNC_TQUEUE -new" "xnvme DuckDB (Write) " "$sf" "$CHAR_DEV"

        if echo "$sf <= 1" | bc -l | grep -q 1; then
                echo "===> CHECKING IF DATABASE IS CORRECT!"
                run_tpch_query "$STANDARD" "Standard DuckDB (Test) tpch query 4" "4" "$sf" "$DB"
                run_tpch_query "$NVME_ASYNC_TQUEUE" "xnvme DuckDB (Test) tpch query 4" "4" "$sf" "$CHAR_DEV"
        fi

        echo "===> READ-INTENSIVE QUERY BENCHMARK! RUNS: $RUNS"
        run_read_benchmark "$STANDARD" "duckdb_standard" "$DB"
        run_read_benchmark "$NVME_FILE -xsync" "duckdb_xnvme_file" "$DB"
        run_read_benchmark "$NVME_SYNC" "duckdb_xnvme_sync" "$DEV"
        run_read_benchmark "$NVME_ASYNC_SQUEUE" "duckdb_xnvme_async_squeue" "$CHAR_DEV"
        run_read_benchmark "$NVME_ASYNC_MQUEUE" "duckdb_xnvme_async_mqueue" "$CHAR_DEV"
        run_read_benchmark "$NVME_ASYNC_TQUEUE" "duckdb_xnvme_async_tqueue" "$CHAR_DEV"
        run_read_benchmark "$NVME_ASYNC_TQUEUE_NO_PASSTHROUGH" "duckdb_xnvme_async_tqueue_no_passthrough" "$DEV"

        remove_existing_db

        echo ""
done

