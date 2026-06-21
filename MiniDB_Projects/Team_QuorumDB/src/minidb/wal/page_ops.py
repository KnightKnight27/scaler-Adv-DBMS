"""Applying a log record's effect to a page.

These two helpers are the single place that knows how a logged change maps to
a physical page edit. They are shared by everything that replays the log:

* recovery REDO (apply the *after* image),
* transaction abort / recovery UNDO (apply the *before* image),
* the replication replica (apply REDO to its own page copy).

Keeping them in one module means redo/undo semantics can never drift apart
between the recovery and replication code paths.
"""

from __future__ import annotations

from ..storage.page import Page
from .log_record import LogRecord, LogType


def redo(record: LogRecord, page: Page) -> None:
    """Re-apply a change (move the page forward in history)."""
    if record.type is LogType.INSERT:
        page.apply_insert(record.slot_no, record.after)
    elif record.type is LogType.DELETE:
        page.apply_delete(record.slot_no)
    elif record.type is LogType.UPDATE:
        page.apply_update(record.slot_no, record.after)
    # BEGIN/COMMIT/ABORT/CHECKPOINT have no page effect.


def undo(record: LogRecord, page: Page) -> None:
    """Apply the compensating change (move the page backward in history)."""
    if record.type is LogType.INSERT:
        page.apply_delete(record.slot_no)
    elif record.type is LogType.DELETE:
        page.apply_insert(record.slot_no, record.before)
    elif record.type is LogType.UPDATE:
        page.apply_update(record.slot_no, record.before)
