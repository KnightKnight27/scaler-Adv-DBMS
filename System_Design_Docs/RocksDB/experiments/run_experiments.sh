#!/bin/bash
set -e

DB_DIR="/tmp/rocksdb_test"
DB_BENCH="/home/harsha/scratch/rocksdb/db_bench"
NUM_KEYS=1000000
VALUE_SIZE=1024

echo "Running tests... this may take a few minutes."

run_compaction_test() {
    STYLE=$1
    NAME=$2
    echo "--- Running $NAME Compaction ---"
    rm -rf $DB_DIR
    
    OUTPUT_FILE="${NAME}_compaction.out"
    
    $DB_BENCH --db=$DB_DIR \
        --benchmarks="fillrandom,overwrite,stats" \
        --use_existing_db=0 \
        --num=$NUM_KEYS \
        --value_size=$VALUE_SIZE \
        --compression_type=none \
        --compaction_style=$STYLE \
        --statistics=1 \
        --stats_dump_period_sec=0 > $OUTPUT_FILE 2>&1
        
    echo "$NAME DB Size:" >> $OUTPUT_FILE
    du -sh $DB_DIR >> $OUTPUT_FILE
}

# 1. Compaction styles and Write/Space Amplification
run_compaction_test 0 "Leveled"
run_compaction_test 1 "Universal"
run_compaction_test 2 "FIFO"

# 2. Read amplification / Bloom Filter effect
run_bloom_test() {
    BITS=$1
    NAME=$2
    echo "--- Running Read Test $NAME ---"
    rm -rf $DB_DIR
    
    OUTPUT_FILE="Bloom_${NAME}.out"
    
    $DB_BENCH --db=$DB_DIR \
        --benchmarks="fillrandom,readrandom,readwhilewriting,stats" \
        --use_existing_db=0 \
        --num=500000 \
        --value_size=$VALUE_SIZE \
        --compression_type=none \
        --bloom_bits=$BITS \
        --statistics=1 \
        --histogram=1 > $OUTPUT_FILE 2>&1
}

run_bloom_test 10 "Enabled"
run_bloom_test -1 "Disabled"

echo "All tests completed!"
