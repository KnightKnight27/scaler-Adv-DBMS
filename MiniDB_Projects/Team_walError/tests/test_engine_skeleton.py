"""Smoke tests for the end-to-end skeleton (build step 1).

These pin the stable public contract of the facade. They must keep passing as
real subsystems replace the stub internals.
"""

import io

import pytest

from minidb import Database, Result
from minidb.engine import MiniDBError


def test_database_opens_and_closes_in_memory():
    db = Database(":memory:")
    db.close()
    with pytest.raises(MiniDBError):
        db.execute("PING")  # closed DB rejects work


def test_ping_proves_pipe_is_wired():
    with Database(":memory:") as db:
        r = db.execute("PING")
    assert isinstance(r, Result)
    assert r.columns == ["pong"]
    assert r.rows == [("pong",)]
    assert r.rowcount == 1


def test_empty_statement_is_ok():
    with Database() as db:
        assert "OK" in db.execute("   ;  ").message


def test_select_from_missing_table_raises_clear_error():
    with Database() as db:
        with pytest.raises(MiniDBError, match="no such table"):
            db.execute("SELECT * FROM t")


def test_result_str_renders_table():
    r = Result(columns=["id", "name"], rows=[(1, "ada"), (2, None)], rowcount=2)
    text = str(r)
    assert "id | name" in text
    assert "ada" in text
    assert "NULL" in text  # None renders as NULL
    assert "2 rows" in text


def test_cli_repl_runs_end_to_end():
    """Drive the REPL with scripted input; prove it speaks to the engine."""
    from minidb.cli import run_repl

    inp = io.StringIO("PING\n.exit\n")
    out = io.StringIO()
    with Database() as db:
        run_repl(db, inp=inp, out=out)
    printed = out.getvalue()
    assert "MiniDB v0.1.0" in printed
    assert "pong" in printed
