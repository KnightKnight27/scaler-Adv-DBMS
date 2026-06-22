# postgres internals

postgres 16 in docker (`popsearch-postgres`), db `popsearch`

## mvcc
- made small table `mvcc_demo`, updated a row twice
- watched `ctid` change each time — postgres doesn't overwrite in place
- `dead` tuple count hit 1 before vacuum cleaned it
- sql in `mvcc.sql`, output in `mvcc-output.txt`

## explain analyze join
- 3 tables: students/departments/enrollments (already had data from earlier)
- ran join query twice: cold (after DISCARD ALL) = 1.99ms, warm = 1.04ms
- all buffer hits, no disk reads
- planner used hash joins + seq scans
- sql in `explain-join.sql`, output in `explain-join-output.txt`
