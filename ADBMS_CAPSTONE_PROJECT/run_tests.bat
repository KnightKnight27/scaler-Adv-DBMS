del /Q minidb_primary.db minidb_replica.db wal_primary.log wal_replica.log
minidb.exe < test_persist2.sql
minidb.exe < test_restart2.sql
