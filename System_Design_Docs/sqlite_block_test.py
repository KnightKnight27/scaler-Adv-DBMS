# ~/sqlite_block_test.py
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
    conn.execute(f'PRAGMA journal_mode = {mode};')
    cur = conn.cursor()
    cur.execute('BEGIN TRANSACTION;')
    cur.execute('INSERT INTO t(a) VALUES (1);')
    # hold the transaction open
    time.sleep(hold_seconds)
    conn.commit()
    conn.close()

def reader():
    conn = sqlite3.connect(DB, timeout=30)
    cur = conn.cursor()
    t0 = time.time()
    try:
        cur.execute('SELECT count(*) FROM t;')
        r = cur.fetchone()[0]
        took = time.time() - t0
        print(f'reader returned rows={r} time={took:.3f}s')
    except Exception as e:
        print('reader exception', e)
    conn.close()

def run_test(mode):
    print('--- TEST MODE', mode, '---')
    setup()
    th = threading.Thread(target=writer, args=(mode, 6))
    th.start()
    # ensure writer started and holds tx
    time.sleep(1)
    reader()
    th.join()
    # cleanup for next run
    conn = sqlite3.connect(DB)
    conn.execute('DELETE FROM t;')
    conn.commit()
    conn.close()

if __name__ == '__main__':
    run_test('DELETE')
    run_test('WAL')