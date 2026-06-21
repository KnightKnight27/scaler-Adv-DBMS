import { ASTSelect } from './Parser';

export interface PlanNode {
  type: 'SeqScan' | 'IndexScan' | 'Filter' | 'NestedLoopJoin' | 'Projection';
  cost: number;
  estRows: number;
  details: string;
}

export class CostBasedOptimizer {
  static selectBestPlan(
    ast: ASTSelect,
    tableSize: number,
    hasIndex: boolean,
    indexKeyPresent: boolean
  ): PlanNode {
    let selectivity = 1.0;
    if (ast.where) {
      if (ast.where.op === '=') {
        selectivity = 1 / Math.max(1, tableSize);
      } else {
        selectivity = 0.3;
      }
    }
    const estRows = Math.ceil(tableSize * selectivity);

    let scanType: 'SeqScan' | 'IndexScan' = 'SeqScan';
    let scanCost = tableSize * 1.0;

    if (hasIndex && indexKeyPresent) {
      const indexScanCost = 2.0 + (estRows * 1.2);
      if (indexScanCost < scanCost) {
        scanType = 'IndexScan';
        scanCost = indexScanCost;
      }
    }

    return {
      type: scanType,
      cost: scanCost,
      estRows,
      details: `${scanType === 'IndexScan' ? 'Index Scan using PK B+ Tree' : 'Full Table Scan'} (Cost: ${scanCost.toFixed(2)}, Est Rows: ${estRows})`
    };
  }
}
