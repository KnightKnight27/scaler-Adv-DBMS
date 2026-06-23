#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOC_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT_DIR="$(cd "$DOC_DIR/../.." && pwd)"
WORK_DIR="$ROOT_DIR/.local/rocksdb"
DB_DIR="$WORK_DIR/lsm-demo"
OUTPUT="$DOC_DIR/EXPERIMENT_RESULTS.md"

rm -rf "$WORK_DIR"
mkdir -p "$DB_DIR"

LDB=(rocksdb_ldb --db="$DB_DIR" --write_buffer_size=4096 --bloom_bits=10 --auto_compaction=false)

for n in $(seq -w 1 300); do
  "${LDB[@]}" put "user:$n" "city=Bengaluru;tier=standard;balance=$n" --create_if_missing >/dev/null
done

scan_before="$WORK_DIR/scan_before.txt"
files_before_raw="$WORK_DIR/files_before_raw.txt"
files_before="$WORK_DIR/files_before.txt"
props_before="$WORK_DIR/properties_before.txt"
compact_log="$WORK_DIR/compact.txt"
files_after_raw="$WORK_DIR/files_after_raw.txt"
files_after="$WORK_DIR/files_after.txt"
stats_after="$WORK_DIR/stats_after.txt"
range_size="$WORK_DIR/range_size.txt"
point_lookup="$WORK_DIR/point_lookup.txt"

rocksdb_ldb --db="$DB_DIR" scan --from="user:001" --to="user:010" --max_keys=10 > "$scan_before"
rocksdb_ldb --db="$DB_DIR" list_live_files_metadata > "$files_before_raw"
{
  printf "Total SST files: %s\n" "$(grep -c '\.sst' "$files_before_raw" || true)"
  printf "Representative live-file metadata:\n"
  sed -n '1,28p' "$files_before_raw"
} > "$files_before"
{
  rocksdb_ldb --db="$DB_DIR" get_property rocksdb.num-entries-active-mem-table
  rocksdb_ldb --db="$DB_DIR" get_property rocksdb.num-immutable-mem-table
  rocksdb_ldb --db="$DB_DIR" get_property rocksdb.estimate-num-keys
  rocksdb_ldb --db="$DB_DIR" get_property rocksdb.estimate-pending-compaction-bytes
} > "$props_before"

rocksdb_ldb --db="$DB_DIR" compact > "$compact_log"
rocksdb_ldb --db="$DB_DIR" list_live_files_metadata > "$files_after_raw"
{
  printf "Total SST files: %s\n" "$(grep -c '\.sst' "$files_after_raw" || true)"
  printf "Representative live-file metadata:\n"
  sed -n '1,28p' "$files_after_raw"
} > "$files_after"
rocksdb_ldb --db="$DB_DIR" get_property rocksdb.stats > "$stats_after"
rocksdb_ldb --db="$DB_DIR" approxsize --from="user:001" --to="user:300" > "$range_size"
rocksdb_ldb --db="$DB_DIR" get "user:128" > "$point_lookup"

{
  printf "# RocksDB Experiment Results\n\n"
  printf "Generated locally by \`experiments/run_experiments.sh\`.\n\n"
  printf "## Tool Version\n\n"
  printf "\`\`\`text\n"
  rocksdb_ldb --version 2>/dev/null || printf "rocksdb_ldb available at %s\n" "$(command -v rocksdb_ldb)"
  printf "\`\`\`\n\n"
  printf "## Workload\n\n"
  printf "%s\n" "- Inserted 300 sorted keys named \`user:001\` through \`user:300\`."
  printf "%s\n" "- Opened the DB with a 4 KB write buffer, Bloom filters, and automatic compaction disabled."
  printf "%s\n\n" "- Forced manual compaction after observing the loaded database."
  printf "## Scan Sample\n\n"
  printf "\`\`\`text\n"
  cat "$scan_before"
  printf "\`\`\`\n\n"
  printf "## Properties Before Manual Compaction\n\n"
  printf "\`\`\`text\n"
  cat "$props_before"
  printf "\`\`\`\n\n"
  printf "## Live Files Before Manual Compaction\n\n"
  printf "\`\`\`text\n"
  cat "$files_before"
  printf "\`\`\`\n\n"
  printf "## Live Files After Manual Compaction\n\n"
  printf "\`\`\`text\n"
  cat "$files_after"
  printf "\`\`\`\n\n"
  printf "## Range Size Estimate\n\n"
  printf "\`\`\`text\n"
  cat "$range_size"
  printf "\`\`\`\n\n"
  printf "## Point Lookup\n\n"
  printf "\`\`\`text\n"
  cat "$point_lookup"
  printf "\`\`\`\n\n"
  printf "## RocksDB Stats After Compaction\n\n"
  printf "\`\`\`text\n"
  cat "$stats_after"
  printf "\`\`\`\n"
} > "$OUTPUT"

printf "Wrote %s\n" "$OUTPUT"
