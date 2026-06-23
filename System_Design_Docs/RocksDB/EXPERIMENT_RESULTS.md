# RocksDB Experiment Results

Generated locally by `experiments/run_experiments.sh`.

## Tool Version

```text
ldb from RocksDB 11.1.1
```

## Workload

- Inserted 300 sorted keys named `user:001` through `user:300`.
- Opened the DB with a 4 KB write buffer, Bloom filters, and automatic compaction disabled.
- Forced manual compaction after observing the loaded database.

## Scan Sample

```text
user:001 ==> city=Bengaluru;tier=standard;balance=001
user:002 ==> city=Bengaluru;tier=standard;balance=002
user:003 ==> city=Bengaluru;tier=standard;balance=003
user:004 ==> city=Bengaluru;tier=standard;balance=004
user:005 ==> city=Bengaluru;tier=standard;balance=005
user:006 ==> city=Bengaluru;tier=standard;balance=006
user:007 ==> city=Bengaluru;tier=standard;balance=007
user:008 ==> city=Bengaluru;tier=standard;balance=008
user:009 ==> city=Bengaluru;tier=standard;balance=009
```

## Properties Before Manual Compaction

```text
rocksdb.num-entries-active-mem-table: 1
rocksdb.num-immutable-mem-table: 0
rocksdb.estimate-num-keys: 300
rocksdb.estimate-pending-compaction-bytes: 427292
```

## Live Files Before Manual Compaction

```text
Total SST files: 299
Representative live-file metadata:
===== Column Family: default =====
Live SST Files:
---------- level 0 ----------
/Users/daksh/mySpace/code/ADBMS project/system design pj/.local/rocksdb/lsm-demo/001498.sst
/Users/daksh/mySpace/code/ADBMS project/system design pj/.local/rocksdb/lsm-demo/001493.sst
/Users/daksh/mySpace/code/ADBMS project/system design pj/.local/rocksdb/lsm-demo/001488.sst
/Users/daksh/mySpace/code/ADBMS project/system design pj/.local/rocksdb/lsm-demo/001483.sst
/Users/daksh/mySpace/code/ADBMS project/system design pj/.local/rocksdb/lsm-demo/001478.sst
/Users/daksh/mySpace/code/ADBMS project/system design pj/.local/rocksdb/lsm-demo/001473.sst
/Users/daksh/mySpace/code/ADBMS project/system design pj/.local/rocksdb/lsm-demo/001468.sst
/Users/daksh/mySpace/code/ADBMS project/system design pj/.local/rocksdb/lsm-demo/001463.sst
/Users/daksh/mySpace/code/ADBMS project/system design pj/.local/rocksdb/lsm-demo/001458.sst
/Users/daksh/mySpace/code/ADBMS project/system design pj/.local/rocksdb/lsm-demo/001453.sst
/Users/daksh/mySpace/code/ADBMS project/system design pj/.local/rocksdb/lsm-demo/001448.sst
/Users/daksh/mySpace/code/ADBMS project/system design pj/.local/rocksdb/lsm-demo/001443.sst
/Users/daksh/mySpace/code/ADBMS project/system design pj/.local/rocksdb/lsm-demo/001438.sst
/Users/daksh/mySpace/code/ADBMS project/system design pj/.local/rocksdb/lsm-demo/001433.sst
/Users/daksh/mySpace/code/ADBMS project/system design pj/.local/rocksdb/lsm-demo/001428.sst
/Users/daksh/mySpace/code/ADBMS project/system design pj/.local/rocksdb/lsm-demo/001423.sst
/Users/daksh/mySpace/code/ADBMS project/system design pj/.local/rocksdb/lsm-demo/001418.sst
/Users/daksh/mySpace/code/ADBMS project/system design pj/.local/rocksdb/lsm-demo/001413.sst
/Users/daksh/mySpace/code/ADBMS project/system design pj/.local/rocksdb/lsm-demo/001408.sst
/Users/daksh/mySpace/code/ADBMS project/system design pj/.local/rocksdb/lsm-demo/001403.sst
/Users/daksh/mySpace/code/ADBMS project/system design pj/.local/rocksdb/lsm-demo/001398.sst
/Users/daksh/mySpace/code/ADBMS project/system design pj/.local/rocksdb/lsm-demo/001393.sst
/Users/daksh/mySpace/code/ADBMS project/system design pj/.local/rocksdb/lsm-demo/001388.sst
/Users/daksh/mySpace/code/ADBMS project/system design pj/.local/rocksdb/lsm-demo/001383.sst
/Users/daksh/mySpace/code/ADBMS project/system design pj/.local/rocksdb/lsm-demo/001378.sst
```

## Live Files After Manual Compaction

```text
Total SST files: 1
Representative live-file metadata:
===== Column Family: default =====
Live SST Files:
---------- level 0 ----------
---------- level 1 ----------
---------- level 2 ----------
---------- level 3 ----------
---------- level 4 ----------
---------- level 5 ----------
---------- level 6 ----------
/Users/daksh/mySpace/code/ADBMS project/system design pj/.local/rocksdb/lsm-demo/001508.sst
Live Blob Files:
------------------------------
```

## Range Size Estimate

```text
3056
```

## Point Lookup

```text
city=Bengaluru;tier=standard;balance=128
```

## RocksDB Stats After Compaction

```text
rocksdb.stats: 
** Compaction Stats [default] **
Level    Files   Size     Score Read(GB)  Rn(GB) Rnp1(GB) Write(GB) WPreComp(GB) Wnew(GB) Moved(GB) W-Amp Rd(MB/s) Wr(MB/s) Comp(sec) CompMergeCPU(sec) Comp(cnt) Avg(sec) KeyIn KeyDrop Rblob(GB) Wblob(GB)
-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
  L6      1/0      3.89 KB   0.0      0.0     0.0      0.0       0.0       0.0      0.0       0.0   0.0      0.0      0.0      0.00              0.00         0    0.000       0      0       0.0       0.0
 Sum      1/0      3.89 KB   0.0      0.0     0.0      0.0       0.0       0.0      0.0       0.0   0.0      0.0      0.0      0.00              0.00         0    0.000       0      0       0.0       0.0
 Int      0/0      0.00 KB   0.0      0.0     0.0      0.0       0.0       0.0      0.0       0.0   0.0      0.0      0.0      0.00              0.00         0    0.000       0      0       0.0       0.0

** Compaction Stats [default] **
Priority    Files   Size     Score Read(GB)  Rn(GB) Rnp1(GB) Write(GB) WPreComp(GB) Wnew(GB) Moved(GB) W-Amp Rd(MB/s) Wr(MB/s) Comp(sec) CompMergeCPU(sec) Comp(cnt) Avg(sec) KeyIn KeyDrop Rblob(GB) Wblob(GB)
----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

Blob file count: 0, total size: 0.0 GB, garbage size: 0.0 GB, space amp: 0.0

Uptime(secs): 0.0 total, 0.0 interval
Flush(GB): cumulative 0.000, interval 0.000
AddFile(GB): cumulative 0.000, interval 0.000
AddFile(Total Files): cumulative 0, interval 0
AddFile(L0 Files): cumulative 0, interval 0
AddFile(Keys): cumulative 0, interval 0
Cumulative compaction: 0.00 GB write, 0.00 MB/s write, 0.00 GB read, 0.00 MB/s read, 0.0 seconds
Interval compaction: 0.00 GB write, 0.00 MB/s write, 0.00 GB read, 0.00 MB/s read, 0.0 seconds
Estimated pending compaction bytes: 0
Write Stall (count): cf-l0-file-count-limit-delays-with-ongoing-compaction: 0, cf-l0-file-count-limit-stops-with-ongoing-compaction: 0, l0-file-count-limit-delays: 0, l0-file-count-limit-stops: 0, memtable-limit-delays: 0, memtable-limit-stops: 0, pending-compaction-bytes-delays: 0, pending-compaction-bytes-stops: 0, total-delays: 0, total-stops: 0

** File Read Latency Histogram By Level [default] **

** DB Stats **
Uptime(secs): 0.0 total, 0.0 interval
Cumulative writes: 0 writes, 0 keys, 0 commit groups, 0.0 writes per commit group, ingest: 0.00 GB, 0.00 MB/s
Cumulative WAL: 0 writes, 0 syncs, 0.00 writes per sync, written: 0.00 GB, 0.00 MB/s
Cumulative stall: 00:00:0.000 H:M:S, 0.0 percent
Interval writes: 0 writes, 0 keys, 0 commit groups, 0.0 writes per commit group, ingest: 0.00 MB, 0.00 MB/s
Interval WAL: 0 writes, 0 syncs, 0.00 writes per sync, written: 0.00 GB, 0.00 MB/s
Interval stall: 00:00:0.000 H:M:S, 0.0 percent
Write Stall (count): write-buffer-manager-limit-stops: 0

```
