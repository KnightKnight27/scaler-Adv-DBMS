package lsm

import "sort"

// mergeRuns merges several sorted runs into one sorted run of unique keys. The
// runs must be ordered newest-first so the newest value for each key wins. When
// dropTombstones is true (a full compaction down to the base level), tombstones
// are discarded because there is no older data left to shadow.
func mergeRuns(runs [][]KV, dropTombstones bool) []KV {
	latest := map[string]KV{}
	for _, run := range runs { // newest first
		for _, kv := range run {
			if _, seen := latest[kv.Key]; !seen {
				latest[kv.Key] = kv
			}
		}
	}
	out := make([]KV, 0, len(latest))
	for _, kv := range latest {
		if dropTombstones && kv.Tomb {
			continue
		}
		out = append(out, kv)
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Key < out[j].Key })
	return out
}
