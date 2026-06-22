import sqlite3
import time
import os
import subprocess

def run_sqlite_test():
    db_path = "Lab 2/sample.db"
    
    # 1. File size
    file_size = os.path.getsize(db_path)
    print(f"--- File Info ---")
    print(f"Database File: {db_path}")
    print(f"Size: {file_size / (1024*1024):.2f} MB")
    
    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()
    
    # 2. PRAGMA page_size
    cursor.execute("PRAGMA page_size")
    page_size = cursor.fetchone()[0]
    print(f"\n--- PRAGMA info ---")
    print(f"Page Size: {page_size} bytes")
    
    # 3. PRAGMA page_count
    cursor.execute("PRAGMA page_count")
    page_count = cursor.fetchone()[0]
    print(f"Page Count: {page_count}")
    
    # 4. PRAGMA mmap_size
    cursor.execute("PRAGMA mmap_size")
    mmap_size = cursor.fetchone()[0]
    print(f"Initial mmap_size: {mmap_size}")
    
    # 5. Timing query without mmap (default is often 0 or small)
    print(f"\n--- Performance Testing ---")
    
    # Ensure mmap is off
    cursor.execute("PRAGMA mmap_size = 0")
    start_time = time.time()
    cursor.execute("SELECT * FROM users")
    cursor.fetchall()
    end_time = time.time()
    time_without_mmap = end_time - start_time
    print(f"Time without mmap: {time_without_mmap:.4f} seconds")
    
    # Ensure mmap is on (set to 256MB)
    cursor.execute("PRAGMA mmap_size = 268435456")
    start_time = time.time()
    cursor.execute("SELECT * FROM users")
    cursor.fetchall()
    end_time = time.time()
    time_with_mmap = end_time - start_time
    print(f"Time with mmap (256MB): {time_with_mmap:.4f} seconds")
    
    conn.close()

if __name__ == "__main__":
    run_sqlite_test()
