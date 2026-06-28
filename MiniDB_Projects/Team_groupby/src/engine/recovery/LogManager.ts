import { LogRecord, Tuple } from '../types';

export class LogManager {
  private log: LogRecord[] = [];
  private currentLsn = 1;

  getLogs(): LogRecord[] {
    return this.log;
  }

  appendRecord(
    txnId: number,
    type: 'BEGIN' | 'COMMIT' | 'ABORT' | 'INSERT' | 'DELETE' | 'UPDATE',
    tableName?: string,
    pageId?: number,
    slotId?: number,
    oldTuple?: Tuple,
    newTuple?: Tuple
  ): number {
    const lsn = this.currentLsn++;
    const record: LogRecord = {
      lsn,
      txnId,
      type,
      tableName,
      pageId,
      slotId,
      oldTuple: oldTuple ? JSON.parse(JSON.stringify(oldTuple)) : undefined,
      newTuple: newTuple ? JSON.parse(JSON.stringify(newTuple)) : undefined
    };
    this.log.push(record);
    return lsn;
  }

  clearLogs() {
    this.log = [];
    this.currentLsn = 1;
  }
}
