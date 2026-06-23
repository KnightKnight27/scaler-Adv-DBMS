# ~/sqlite_block_test_forced.py
import sqlite3, time, threading

DB = 'test_sqlite.db'

def setup():
    conn = sqlite3.connect(DB)
    conn.execute('CREATE TABLE IF NOT EXISTS t(a INTEGER);')
    conn.execute('DELETE FROM t;')
    conn.commit()
    conn.close()

def writer(mode, hold_seconds):
    conn = sqlite3.connect(DB, timeout=30)
    # Ensure PRAGMA is set on this connection
    conn.execute(f'PRAGMA journal_mode = {mode};')
    cur = conn.cursor()
    # BEGIN EXCLUSIVE will obtain an exclusive lock immediately (prevents readers in DELETE mode)
    cur.execute('BEGIN EXCLUSIVE;')
    cur.execute('INSERT INTO t(a) VALUES (1);')
    print(f'[writer-{mode}] started, holding tx for {hold_seconds}s')
    time.sleep(hold_seconds)
    conn.commit()
    conn.close()
    print(f'[writer-{mode}] committed')

def reader():
    conn = sqlite3.connect(DB, timeout=30)
    cur = conn.cursor()
    t0 = time.time()
    try:
        cur.execute('SELECT count(*) FROM t;')
        r = cur.fetchone()[0]
        took = time.time() - t0
        print(f'[reader] returned rows={r} time={took:.3f}s')
    except Exception as e:
        print('reader exception', e)
    conn.close()

def run_test(mode):
    print('--- TEST MODE', mode, '---')
    setup()
    th = threading.Thread(target=writer, args=(mode, 6))
    th.start()
    # wait a moment for writer to start and acquire lock
    time.sleep(1)
    reader()
    th.join()
    # cleanup
    conn = sqlite3.connect(DB)
    conn.execute('DELETE FROM t;')
    conn.commit()
    conn.close()

if __name__ == '__main__':
    run_test('DELETE')
    run_test('WAL')