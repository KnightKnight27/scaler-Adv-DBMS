# M5 Recovery Notes

The recovery benchmark is correctness-oriented because M5 evaluates crash
recovery semantics more than raw speed.

## Metrics To Capture Locally

- WAL append latency for `BEGIN`, row change, and `COMMIT`.
- Recovery time as WAL record count grows.
- Number of committed transactions replayed.
- Number of uncommitted or aborted transactions ignored.

## Expected Analysis

- Recovery work is linear in WAL size because checkpointing is not implemented.
- Committed changes are preserved after restart.
- Uncommitted changes are not visible after restart.
- Delete records are replayed after insert records, so committed deletes remove
  previously committed rows.

## Current Status

The repository contains executable M5 tests for committed redo, uncommitted
transaction filtering, and committed delete replay. Numeric latency results
should be filled after running the test binary on the local machine.
