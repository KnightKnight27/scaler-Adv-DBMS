from __future__ import annotations

import shutil
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
BENCHMARKS = ROOT / "benchmarks"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))
if str(BENCHMARKS) not in sys.path:
    sys.path.insert(0, str(BENCHMARKS))

from minidb.engine import MiniDBEngine
from minidb.parser import (
    BeginStatement,
    CommitStatement,
    CreateIndexStatement,
    CreateTableStatement,
    DeleteStatement,
    RollbackStatement,
    SQLParser,
    SelectStatement,
    SetModeStatement,
)
from minidb.types import TransactionMode
from run_concurrency_comparison import Workload, run_workload

TEST_RUNTIME_ROOT = ROOT / ".test_runtime_submission"
TEST_RUNTIME_ROOT.mkdir(exist_ok=True)


def make_runtime_dir(name: str) -> Path:
    path = TEST_RUNTIME_ROOT / name
    if path.exists():
        shutil.rmtree(path, ignore_errors=True)
    path.mkdir(parents=True, exist_ok=True)
    return path


class ParserCoverageTests(unittest.TestCase):
    def setUp(self) -> None:
        self.parser = SQLParser()

    def test_parser_supports_required_submission_statements(self) -> None:
        statements = [
            self.parser.parse("CREATE TABLE accounts (id INT, name TEXT, balance INT);"),
            self.parser.parse("CREATE INDEX idx_accounts_id ON accounts(id);"),
            self.parser.parse("EXPLAIN SELECT * FROM accounts WHERE id = 1;"),
            self.parser.parse(
                "SELECT * FROM accounts JOIN transactions ON accounts.id = transactions.account_id;"
            ),
            self.parser.parse("DELETE FROM accounts WHERE id = 1;"),
            self.parser.parse("BEGIN;"),
            self.parser.parse("COMMIT;"),
            self.parser.parse("ROLLBACK;"),
            self.parser.parse("SET MODE 2PL;"),
            self.parser.parse("SET MODE MVCC;"),
        ]
        self.assertIsInstance(statements[0], CreateTableStatement)
        self.assertIsInstance(statements[1], CreateIndexStatement)
        self.assertIsInstance(statements[2], SelectStatement)
        self.assertTrue(statements[2].explain)
        self.assertIsInstance(statements[3], SelectStatement)
        self.assertIsNotNone(statements[3].join)
        self.assertIsInstance(statements[4], DeleteStatement)
        self.assertIsInstance(statements[5], BeginStatement)
        self.assertIsInstance(statements[6], CommitStatement)
        self.assertIsInstance(statements[7], RollbackStatement)
        self.assertIsInstance(statements[8], SetModeStatement)
        self.assertEqual(statements[8].mode, TransactionMode.TWO_PL)
        self.assertEqual(statements[9].mode, TransactionMode.MVCC)


class SubmissionSqlFlowTests(unittest.TestCase):
    def test_required_sql_flow_runs_end_to_end(self) -> None:
        runtime_dir = make_runtime_dir(self._testMethodName)
        engine = MiniDBEngine(runtime_dir)

        engine.execute("CREATE TABLE accounts (id INT, name TEXT, balance INT);")
        engine.execute("CREATE TABLE transactions (id INT, account_id INT, amount INT);")
        engine.execute("CREATE INDEX idx_accounts_id ON accounts(id);")

        engine.execute("INSERT INTO accounts VALUES (1, 'Asha', 1000);")
        engine.execute("INSERT INTO accounts VALUES (2, 'Bina', 750);")
        engine.execute("INSERT INTO transactions VALUES (11, 1, 200);")
        engine.execute("INSERT INTO transactions VALUES (12, 2, 125);")

        explain = engine.execute("EXPLAIN SELECT * FROM accounts WHERE id = 1;")
        select_rows = engine.execute("SELECT * FROM accounts WHERE id = 1;")
        join_rows = engine.execute(
            "SELECT * FROM accounts JOIN transactions ON accounts.id = transactions.account_id;"
        )

        self.assertIn("INDEX_SCAN", explain)
        self.assertEqual(select_rows, [{"id": 1, "name": "Asha", "balance": 1000}])
        self.assertEqual(len(join_rows), 2)

        engine.execute("SET MODE 2PL;")
        engine.execute("BEGIN;")
        engine.execute("DELETE FROM accounts WHERE id = 2;")
        engine.execute("ROLLBACK;")
        self.assertEqual(
            engine.execute("SELECT * FROM accounts WHERE id = 2;"),
            [{"id": 2, "name": "Bina", "balance": 750}],
        )

        engine.execute("SET MODE MVCC;")
        engine.execute("BEGIN;")
        engine.execute("DELETE FROM accounts WHERE id = 1;")
        engine.execute("INSERT INTO accounts VALUES (1, 'Asha', 900);")
        engine.execute("COMMIT;")
        self.assertEqual(
            engine.execute("SELECT * FROM accounts WHERE id = 1;"),
            [{"id": 1, "name": "Asha", "balance": 900}],
        )


class BenchmarkSmokeTests(unittest.TestCase):
    def test_concurrency_benchmark_smoke(self) -> None:
        workload = Workload(
            name="smoke_submission",
            readers_per_writer=1,
            writers_per_round=1,
            rounds=1,
            hold_seconds=0.005,
            hot_keys=(1,),
            description="Submission smoke workload.",
        )
        result_2pl = run_workload(TransactionMode.TWO_PL, workload)
        result_mvcc = run_workload(TransactionMode.MVCC, workload)

        self.assertGreater(float(result_2pl["throughput_tps"]), 0.0)
        self.assertGreater(float(result_mvcc["throughput_tps"]), 0.0)
        self.assertIn("version_count", result_2pl)
        self.assertIn("version_count", result_mvcc)
        self.assertEqual(str(result_2pl["mode"]), "2PL")
        self.assertEqual(str(result_mvcc["mode"]), "MVCC")


if __name__ == "__main__":
    unittest.main()
