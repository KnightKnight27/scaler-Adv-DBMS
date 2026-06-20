import { LogRecord, Tuple } from '../types';
import { BufferPoolManager } from '../storage/BufferPoolManager';
import { PageManager } from '../storage/PageManager';

export class RecoveryManager {
  static performARIESRecovery(
    logs: LogRecord[],
    bufferPool: BufferPoolManager,
    committedTxns: Set<number>
  ): { redoCount: number; undoCount: number; recoverySteps: string[] } {
    const steps: string[] = [];
    let redoCount = 0;
    let undoCount = 0;

    steps.push("[ANALYSIS] Scanning Write-Ahead Log forward to identify active transactions...");
    const activeTxns = new Set<number>();
    for (const log of logs) {
      if (log.type === 'BEGIN') {
        activeTxns.add(log.txnId);
      } else if (log.type === 'COMMIT') {
        activeTxns.delete(log.txnId);
        committedTxns.add(log.txnId);
      } else if (log.type === 'ABORT') {
        activeTxns.delete(log.txnId);
      }
    }
    steps.push(`[ANALYSIS] Complete. Active Transactions to UNDO: ${Array.from(activeTxns).join(', ') || 'None'}`);

    steps.push("[REDO] Repeating log history from first LSN to bring database to consistent disk state...");
    for (const log of logs) {
      if (log.type === 'INSERT' && log.pageId !== undefined && log.newTuple) {
        const page = bufferPool.fetchPage(log.pageId);
        PageManager.updateTuple(page, log.slotId!, log.newTuple);
        bufferPool.unpinPage(log.pageId, true);
        redoCount++;
        steps.push(`[REDO] LSN ${log.lsn}: Restored INSERT of key ${log.newTuple.id} on page ${log.pageId}`);
      } else if (log.type === 'DELETE' && log.pageId !== undefined) {
        const page = bufferPool.fetchPage(log.pageId);
        if (log.newTuple) {
          PageManager.updateTuple(page, log.slotId!, log.newTuple);
        } else {
          PageManager.deleteTuple(page, log.slotId!);
        }
        bufferPool.unpinPage(log.pageId, true);
        redoCount++;
        steps.push(`[REDO] LSN ${log.lsn}: Restored DELETE mark on page ${log.pageId}, slot ${log.slotId}`);
      }
    }

    steps.push("[UNDO] Rolling back uncommitted/active transactions to maintain serialized recovery guarantees...");
    const reverseLogs = [...logs].reverse();
    for (const log of reverseLogs) {
      if (activeTxns.has(log.txnId)) {
        if (log.type === 'INSERT' && log.pageId !== undefined) {
          const page = bufferPool.fetchPage(log.pageId);
          PageManager.deleteTuple(page, log.slotId!);
          bufferPool.unpinPage(log.pageId, true);
          undoCount++;
          steps.push(`[UNDO] LSN ${log.lsn}: Reversed INSERT (key: ${log.newTuple?.id}) by uncommitted transaction ${log.txnId}`);
        } else if (log.type === 'DELETE' && log.pageId !== undefined && log.oldTuple) {
          const page = bufferPool.fetchPage(log.pageId);
          PageManager.updateTuple(page, log.slotId!, log.oldTuple);
          bufferPool.unpinPage(log.pageId, true);
          undoCount++;
          steps.push(`[UNDO] LSN ${log.lsn}: Restored deleted record (key: ${log.oldTuple.id}) of uncommitted transaction ${log.txnId}`);
        }
      }
    }

    steps.push("[RECOVERY] Recovery protocol completed. Database is now consistent and safe!");
    return { redoCount, undoCount, recoverySteps: steps };
  }
}
