import { Transaction, Tuple } from '../types';

export class TransactionManager {
  private activeTransactions: Map<number, Transaction> = new Map();
  private transactionCounter = 100;

  beginTransaction(): Transaction {
    const id = this.transactionCounter++;
    const snapshotActiveTxns = Array.from(this.activeTransactions.keys());
    const txn: Transaction = {
      id,
      status: 'ACTIVE',
      snapshotActiveTxns,
      beginLsn: 0
    };
    this.activeTransactions.set(id, txn);
    return txn;
  }

  commitTransaction(txnId: number) {
    const txn = this.activeTransactions.get(txnId);
    if (txn) {
      txn.status = 'COMMITTED';
      this.activeTransactions.delete(txnId);
    }
  }

  abortTransaction(txnId: number) {
    const txn = this.activeTransactions.get(txnId);
    if (txn) {
      txn.status = 'ABORTED';
      this.activeTransactions.delete(txnId);
    }
  }

  static isTupleVisible(
    tuple: Tuple,
    readerTxnId: number,
    activeSnapTxns: number[],
    committedTxns: Set<number>
  ): boolean {
    const xmin = tuple.xmin;
    const xmax = tuple.xmax;

    const isXminCommitted = committedTxns.has(xmin) || xmin === 0 || xmin === readerTxnId;
    if (!isXminCommitted) return false;

    const wasXminActiveAtStart = activeSnapTxns.includes(xmin);
    if (wasXminActiveAtStart && xmin !== readerTxnId) return false;

    if (xmax !== 0) {
      const isXmaxCommitted = committedTxns.has(xmax) || xmax === readerTxnId;
      if (isXmaxCommitted) {
        const wasXmaxActiveAtStart = activeSnapTxns.includes(xmax);
        if (!wasXmaxActiveAtStart || xmax === readerTxnId) {
          return false;
        }
      }
    }
    return true;
  }
}
