import os
import sys
import time
import shutil
import tempfile
import threading
import matplotlib.pyplot as plt

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from run import MiniDB

class BenchmarkRunner:
    def __init__(self):
        self.test_dir = tempfile.mkdtemp()
        self.db = MiniDB(db_dir=self.test_dir, pool_size=1000)
        self.plots_dir = os.path.join(os.path.dirname(__file__), 'plots')
        os.makedirs(self.plots_dir, exist_ok=True)
        self.results = []

    def cleanup(self):
        self.db.buffer_pool.flush_all()
        self.db.wal.close()
        self.db.disk_manager.close()
        shutil.rmtree(self.test_dir, ignore_errors=True)

    def run_all(self):
        print("Starting Benchmark Suite...")
        self._bench_insert_throughput()
        self._bench_scan_vs_index()
        self._bench_mvcc_concurrency()
        self._generate_report()
        self.cleanup()
        print(f"Benchmarks completed. Results saved to {os.path.dirname(__file__)}/benchmark_results.md")

    def _bench_insert_throughput(self):
        print("Running: Insert Throughput...")
        self.db.execute_sql("CREATE TABLE bench_insert (id INTEGER PRIMARY KEY, val VARCHAR)")
        
        batch_sizes = [100, 500, 1000, 2000, 5000]
        times = []
        
        for size in batch_sizes:
            self.db.execute_sql("DELETE FROM bench_insert WHERE id >= 0")
            
            start = time.time()
            self.db.execute_sql("BEGIN")
            for i in range(size):
                self.db.execute_sql(f"INSERT INTO bench_insert (id, val) VALUES ({i}, 'test_data_value')")
            self.db.execute_sql("COMMIT")
            elapsed = time.time() - start
            
            times.append(elapsed)
            
        throughput = [size / t for size, t in zip(batch_sizes, times)]
        
        # Plot
        plt.figure(figsize=(8, 5))
        plt.plot(batch_sizes, throughput, marker='o', linestyle='-', color='b')
        plt.title('Insert Throughput vs Batch Size')
        plt.xlabel('Batch Size (rows)')
        plt.ylabel('Throughput (rows/sec)')
        plt.grid(True)
        plot_path = os.path.join(self.plots_dir, 'insert_throughput.png')
        plt.savefig(plot_path)
        plt.close()
        
        self.results.append({
            'name': 'Insert Throughput',
            'desc': 'Measures rows inserted per second inside a single transaction.',
            'data': list(zip(batch_sizes, throughput)),
            'plot': './plots/insert_throughput.png'
        })

    def _bench_scan_vs_index(self):
        print("Running: Scan vs Index Lookup...")
        self.db.execute_sql("CREATE TABLE bench_scan (id INTEGER PRIMARY KEY, val INTEGER)")
        self.db.execute_sql("CREATE INDEX idx_val ON bench_scan (val)")
        
        # Insert 10k rows
        self.db.execute_sql("BEGIN")
        for i in range(10000):
            self.db.execute_sql(f"INSERT INTO bench_scan (id, val) VALUES ({i}, {i % 1000})")
        self.db.execute_sql("COMMIT")
        self.db.update_statistics('bench_scan')
        
        # Sequential scan (force by querying non-indexed predicate or range)
        start = time.time()
        for i in range(100):
            self.db.execute_sql("SELECT * FROM bench_scan WHERE val >= 0")
        seq_time = time.time() - start
        
        # Index scan (high selectivity)
        start = time.time()
        for i in range(100):
            self.db.execute_sql(f"SELECT * FROM bench_scan WHERE val = {i}")
        idx_time = time.time() - start
        
        # Plot
        labels = ['Sequential Scan (100x)', 'Index Scan (100x)']
        times = [seq_time, idx_time]
        
        plt.figure(figsize=(8, 5))
        plt.bar(labels, times, color=['red', 'green'])
        plt.title('Query Execution Time: Seq Scan vs Index Scan')
        plt.ylabel('Time (seconds)')
        for i, v in enumerate(times):
            plt.text(i, v + 0.05, f"{v:.2f}s", ha='center')
        plot_path = os.path.join(self.plots_dir, 'scan_vs_index.png')
        plt.savefig(plot_path)
        plt.close()
        
        self.results.append({
            'name': 'Sequential vs Index Scan',
            'desc': 'Compares the total time to execute 100 queries using full table scan vs B+ Tree index lookup on a 10,000 row table.',
            'data': list(zip(labels, times)),
            'plot': './plots/scan_vs_index.png'
        })

    def _bench_mvcc_concurrency(self):
        print("Running: MVCC Concurrency Throughput...")
        self.db.execute_sql("CREATE TABLE bench_mvcc (id INTEGER PRIMARY KEY, val INTEGER)")
        self.db.execute_sql("BEGIN")
        for i in range(100):
            self.db.execute_sql(f"INSERT INTO bench_mvcc (id, val) VALUES ({i}, {i})")
        self.db.execute_sql("COMMIT")

        def reader_task(duration):
            end = time.time() + duration
            count = 0
            while time.time() < end:
                self.db.execute_sql("BEGIN")
                self.db.execute_sql("SELECT * FROM bench_mvcc WHERE id = 50")
                self.db.execute_sql("COMMIT")
                count += 1
            return count

        def writer_task(duration):
            end = time.time() + duration
            count = 0
            while time.time() < end:
                self.db.execute_sql("BEGIN")
                self.db.execute_sql(f"UPDATE bench_mvcc SET val = {count} WHERE id = 50")
                self.db.execute_sql("COMMIT")
                count += 1
            return count

        threads = []
        read_counts = []
        write_counts = []
        duration = 5.0

        # Create 5 readers and 2 writers
        for _ in range(5):
            t = threading.Thread(target=lambda: read_counts.append(reader_task(duration)))
            threads.append(t)
        for _ in range(2):
            t = threading.Thread(target=lambda: write_counts.append(writer_task(duration)))
            threads.append(t)

        for t in threads:
            t.start()
        for t in threads:
            t.join()

        total_reads = sum(read_counts)
        total_writes = sum(write_counts)
        
        # Plot
        labels = ['Total Reads', 'Total Writes']
        counts = [total_reads, total_writes]
        
        plt.figure(figsize=(8, 5))
        plt.bar(labels, counts, color=['blue', 'orange'])
        plt.title(f'MVCC Throughput ({duration}s, 5 Readers, 2 Writers)')
        plt.ylabel('Total Transactions Completed')
        for i, v in enumerate(counts):
            plt.text(i, v + total_reads*0.01, str(v), ha='center')
        plot_path = os.path.join(self.plots_dir, 'mvcc_throughput.png')
        plt.savefig(plot_path)
        plt.close()
        
        self.results.append({
            'name': 'MVCC Concurrent Throughput',
            'desc': 'Measures total transactions completed with 5 concurrent readers and 2 concurrent writers operating on the same row over 5 seconds. Demonstrates that MVCC readers do not block writers and vice versa.',
            'data': [('Total Reads', total_reads), ('Total Writes', total_writes), ('Read Throughput (txn/s)', total_reads/duration), ('Write Throughput (txn/s)', total_writes/duration)],
            'plot': './plots/mvcc_throughput.png'
        })

    def _generate_report(self):
        md_path = os.path.join(os.path.dirname(__file__), 'benchmark_results.md')
        with open(md_path, 'w') as f:
            f.write("# MiniDB Performance Benchmarks\n\n")
            f.write("This document contains automated performance benchmarks for the MiniDB engine.\n\n")
            
            for res in self.results:
                f.write(f"## {res['name']}\n\n")
                f.write(f"{res['desc']}\n\n")
                
                f.write("### Data\n\n")
                f.write("| Metric | Value |\n")
                f.write("|--------|-------|\n")
                for label, val in res['data']:
                    if isinstance(val, float):
                        f.write(f"| {label} | {val:.2f} |\n")
                    else:
                        f.write(f"| {label} | {val} |\n")
                f.write("\n")
                
                f.write("### Visualization\n\n")
                f.write(f"![{res['name']}]({res['plot']})\n\n")
                f.write("---\n\n")

if __name__ == '__main__':
    runner = BenchmarkRunner()
    runner.run_all()
