"""Smoke test that the benchmark harness runs and produces sane numbers."""

import importlib.util
import os

import pytest

BENCH = os.path.join(os.path.dirname(__file__), "..", "benchmarks", "run_all.py")


@pytest.fixture(scope="module")
def bench():
    spec = importlib.util.spec_from_file_location("run_all", BENCH)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def test_write_benchmark_reports_all_engines(bench):
    out = bench.bench_writes(2000)
    assert set(out) == {"LSM", "B+Tree", "Heap"}
    assert all(v > 0 for v in out.values())


def test_read_benchmark_reports_latency(bench):
    out = bench.bench_reads(2000, samples=200)
    assert set(out) == {"LSM", "B+Tree"}
    assert all(v > 0 for v in out.values())


def test_space_benchmark_lsm_better_than_heap_under_churn(bench):
    out = bench.bench_space(2000)
    # compaction reclaims; heap tombstones do not -> LSM has lower amplification
    assert out["LSM"] < out["Heap"]
