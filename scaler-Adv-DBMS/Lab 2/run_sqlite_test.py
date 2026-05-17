import sqlite3
import time
import os

def analyze_sqlite_performance():
    database_file = "Lab 2/test_db.sqlite"
    
    # Check file size
    size_in_bytes = os.path.getsize(database_file)
    size_in_mb = size_in_bytes / (1024 * 1024)
    print("========== SQLITE STORAGE METRICS ==========")
    print(f"Target DB: {database_file}")
    print(f"Total Size: {size_in_mb:.2f} MB\n")
    
    # Connect and analyze pragmas
    db_conn = sqlite3.connect(database_file)
    cur = db_conn.cursor()
    
    cur.execute("PRAGMA page_size")
    pg_size = cur.fetchone()[0]
    
    cur.execute("PRAGMA page_count")
    pg_count = cur.fetchone()[0]
    
    cur.execute("PRAGMA mmap_size")
    mmap_initial = cur.fetchone()[0]
    
    print("========== PRAGMA CONFIGURATIONS ==========")
    print(f"SQLite Page Size: {pg_size} bytes")
    print(f"Total Pages: {pg_count}")
    print(f"Default mmap_size: {mmap_initial}\n")
    
    print("========== QUERY BENCHMARKS ==========")
    
    # Test without Memory Mapping
    cur.execute("PRAGMA mmap_size = 0")
    t0 = time.time()
    cur.execute("SELECT * FROM employees")
    cur.fetchall()
    t1 = time.time()
    duration_normal = t1 - t0
    print(f"Execution Time (Standard I/O): {duration_normal:.4f} sec")
    
    # Test with Memory Mapping enabled (128 MB)
    cur.execute("PRAGMA mmap_size = 134217728")
    t0 = time.time()
    cur.execute("SELECT * FROM employees")
    cur.fetchall()
    t1 = time.time()
    duration_mmap = t1 - t0
    print(f"Execution Time (Memory Mapped): {duration_mmap:.4f} sec")
    
    db_conn.close()

if __name__ == "__main__":
    analyze_sqlite_performance()
