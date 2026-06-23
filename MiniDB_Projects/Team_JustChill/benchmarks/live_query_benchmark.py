#!/usr/bin/env python3
import subprocess
import time
import os
import sys

def percentiles(data, p):
    s = sorted(data)
    return s[int(len(s) * p)]

def read_until_done(proc, is_write):
    while True:
        line = proc.stdout.readline()
        if not line:
            break
        if "[ERROR]" in line:
            break
        if is_write and "COMMIT logged" in line:
            break
        if not is_write and "rows)" in line:
            break

def run_once(run_idx, N):
    # Clean up DB files to start fresh
    for f in ["students.dat", "students.idx", "enroll.dat", "enroll.idx", "wal.log"]:
        if os.path.exists(f):
            try: os.remove(f)
            except: pass

    # 1. Start replica in the background
    replica = subprocess.Popen(
        ["stdbuf", "-oL", "-eL", "./MiniDB_Projects/Team_JustChill/build/minidb", "replica", "127.0.0.1", "9999"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        text=True
    )
    time.sleep(0.1)  # wait for replica to bind

    # 2. Start primary
    primary = subprocess.Popen(
        ["stdbuf", "-oL", "-eL", "./MiniDB_Projects/Team_JustChill/build/minidb", "primary", "127.0.0.1", "9999"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True
    )

    # Read startup messages until support info
    while True:
        line = primary.stdout.readline()
        if "EXPLAIN <query>" in line:
            break

    run_results = {}

    # Workload 1: Write Storm (INSERT)
    latencies = []
    for k in range(N):
        sql = f"INSERT INTO students VALUES ({k}, 'user_{k}')\n"
        t0 = time.time()
        primary.stdin.write(sql)
        primary.stdin.flush()
        read_until_done(primary, is_write=True)
        t1 = time.time()
        latencies.append(t1 - t0)
    run_results["Write Storm (INSERT)"] = collect_metrics(latencies)

    # Workload 2: Point Lookups (SELECT PK)
    latencies = []
    for k in range(N):
        sql = f"SELECT * FROM students WHERE id = {k}\n"
        t0 = time.time()
        primary.stdin.write(sql)
        primary.stdin.flush()
        read_until_done(primary, is_write=False)
        t1 = time.time()
        latencies.append(t1 - t0)
    run_results["Point Lookups (SELECT PK)"] = collect_metrics(latencies)

    # Workload 3: Full Table Scans (10 scans)
    latencies = []
    for k in range(10):
        sql = "SELECT * FROM students\n"
        t0 = time.time()
        primary.stdin.write(sql)
        primary.stdin.flush()
        read_until_done(primary, is_write=False)
        t1 = time.time()
        latencies.append(t1 - t0)
    run_results["Full Table Scans (SELECT *)"] = collect_metrics(latencies)

    # Workload 4: Mixed CRUD
    latencies = []
    for k in range(N):
        is_insert = (k % 10 < 3)
        if is_insert:
            sql = f"INSERT INTO students VALUES ({N + k}, 'user_{N + k}')\n"
        else:
            sql = f"SELECT * FROM students WHERE id = {k % N}\n"
        t0 = time.time()
        primary.stdin.write(sql)
        primary.stdin.flush()
        read_until_done(primary, is_write=is_insert)
        t1 = time.time()
        latencies.append(t1 - t0)
    run_results["Mixed CRUD (70/30)"] = collect_metrics(latencies)

    # Clean up primary
    primary.stdin.write("exit\n")
    primary.stdin.flush()
    primary.wait()

    # Clean up replica
    replica.terminate()
    replica.wait()

    # Clean up files
    for f in ["students.dat", "students.idx", "enroll.dat", "enroll.idx", "wal.log"]:
        if os.path.exists(f):
            try: os.remove(f)
            except: pass

    return run_results

def collect_metrics(latencies):
    if not latencies:
        return {"qps": 0.0, "avg": 0.0, "p50": 0.0, "p90": 0.0, "p99": 0.0}
    total = sum(latencies)
    avg_lat = (total / len(latencies)) * 1000000.0  # in microseconds
    p50 = percentiles(latencies, 0.5) * 1000000.0
    p90 = percentiles(latencies, 0.9) * 1000000.0
    p99 = percentiles(latencies, 0.99) * 1000000.0
    qps = len(latencies) / total
    return {"qps": qps, "avg": avg_lat, "p50": p50, "p90": p90, "p99": p99}

def main():
    iterations = 100
    N = 1000  # 1,000 queries per iteration, 100,000 operations total
    print("=============================================")
    print("    MiniDB Live Process SQL Pipeline Benchmark")
    print(f"       ({iterations} Iteration Average, N = {N})")
    print("=============================================\n")

    all_runs = []
    for i in range(iterations):
        run_results = run_once(i, N)
        all_runs.append(run_results)
        if (i + 1) % 10 == 0 or i == 0 or i == iterations - 1:
            print(f"    - Completed {i + 1}/{iterations} iterations...", flush=True)

    print("\n================ FINAL AGGREGATED REPORT =================\n")
    workloads = ["Write Storm (INSERT)", "Point Lookups (SELECT PK)", "Full Table Scans (SELECT *)", "Mixed CRUD (70/30)"]
    for wl in workloads:
        qps_list = [run[wl]["qps"] for run in all_runs]
        avg_list = [run[wl]["avg"] for run in all_runs]
        p50_list = [run[wl]["p50"] for run in all_runs]
        p90_list = [run[wl]["p90"] for run in all_runs]
        p99_list = [run[wl]["p99"] for run in all_runs]

        print(f"  * {wl}:")
        print(f"    - Throughput: {int(sum(qps_list) / len(qps_list))} QPS")
        print(f"    - Latency (Avg): {sum(avg_list) / len(avg_list):.2f} us")
        print(f"    - Latency (p50): {sum(p50_list) / len(p50_list):.2f} us")
        print(f"    - Latency (p90): {sum(p90_list) / len(p90_list):.2f} us")
        print(f"    - Latency (p99): {sum(p99_list) / len(p99_list):.2f} us\n")

if __name__ == "__main__":
    main()
