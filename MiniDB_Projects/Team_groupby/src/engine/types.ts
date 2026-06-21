export interface Tuple {
  id: number;
  values: any[];
  xmin: number;
  xmax: number;
  roll_ptr?: string;
}

export interface PageHeader {
  pageId: number;
  freeSpaceOffset: number;
  slotCount: number;
}

export interface Slot {
  offset: number;
  size: number;
  active: boolean;
}

export interface Page {
  header: PageHeader;
  slots: Slot[];
  data: string[];
}

export interface BufferFrame {
  frameId: number;
  pageId: number | null;
  pinCount: number;
  isDirty: boolean;
  lastAccessed: number;
}

export interface IndexEntry {
  key: number;
  pageId: number;
  slotId: number;
}

export interface Transaction {
  id: number;
  status: 'ACTIVE' | 'COMMITTED' | 'ABORTED';
  snapshotActiveTxns: number[];
  beginLsn: number;
}

export interface LogRecord {
  lsn: number;
  txnId: number;
  type: 'BEGIN' | 'COMMIT' | 'ABORT' | 'INSERT' | 'DELETE' | 'UPDATE';
  tableName?: string;
  pageId?: number;
  slotId?: number;
  oldTuple?: Tuple;
  newTuple?: Tuple;
}
