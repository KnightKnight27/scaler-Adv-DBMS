export interface ASTSelect {
  type: 'SELECT';
  columns: string[];
  table: string;
  joinTable?: string;
  joinOn?: { left: string; right: string };
  where?: { column: string; op: '=' | '>' | '<'; value: any };
}

export interface ASTInsert {
  type: 'INSERT';
  table: string;
  values: any[];
}

export interface ASTDelete {
  type: 'DELETE';
  table: string;
  where?: { column: string; op: '=' | '>' | '<'; value: any };
}

export interface ASTTxn {
  type: 'BEGIN' | 'COMMIT' | 'ROLLBACK';
}

export type AST = ASTSelect | ASTInsert | ASTDelete | ASTTxn;

export class Parser {
  static parse(sql: string): AST {
    const normalized = sql.trim().replace(/\s+/g, ' ');
    const lower = normalized.toUpperCase();

    if (lower.startsWith('BEGIN')) {
      return { type: 'BEGIN' };
    }
    if (lower.startsWith('COMMIT')) {
      return { type: 'COMMIT' };
    }
    if (lower.startsWith('ROLLBACK') || lower.startsWith('ABORT')) {
      return { type: 'ROLLBACK' };
    }

    if (lower.startsWith('INSERT INTO')) {
      const parts = normalized.split(/VALUES/i);
      const tblPart = parts[0].trim().split(' ');
      const table = tblPart[2].replace(/[\(\)]/g, '').trim();
      const valStr = parts[1].replace(/[\(\)]/g, '').trim();
      const values = valStr.split(',').map(v => {
        const val = v.trim();
        if (val.startsWith("'") && val.endsWith("'")) return val.slice(1, -1);
        if (!isNaN(Number(val))) return Number(val);
        return val;
      });
      return { type: 'INSERT', table, values };
    }

    if (lower.startsWith('DELETE FROM')) {
      const parts = normalized.split(/WHERE/i);
      const table = parts[0].trim().split(' ')[2];
      let where: any = undefined;
      if (parts[1]) {
        where = this.parseCondition(parts[1].trim());
      }
      return { type: 'DELETE', table, where };
    }

    if (lower.startsWith('SELECT')) {
      const selectMatch = normalized.match(/SELECT\s+(.+?)\s+FROM\s+(\w+)(?:\s+JOIN\s+(\w+)\s+ON\s+(.+?))?(?:\s+WHERE\s+(.+))?$/i);
      if (!selectMatch) {
        throw new Error("Syntax Error: Unsupported SELECT syntax");
      }
      const columns = selectMatch[1].split(',').map(c => c.trim());
      const table = selectMatch[2].trim();
      const joinTable = selectMatch[3]?.trim();
      const joinOnStr = selectMatch[4]?.trim();
      const whereStr = selectMatch[5]?.trim();

      let joinOn: any = undefined;
      if (joinOnStr) {
        const opParts = joinOnStr.split('=');
        joinOn = { left: opParts[0].trim(), right: opParts[1].trim() };
      }

      let where: any = undefined;
      if (whereStr) {
        where = this.parseCondition(whereStr);
      }

      return { type: 'SELECT', columns, table, joinTable, joinOn, where };
    }

    throw new Error("Syntax Error: Unknown query command");
  }

  private static parseCondition(cond: string) {
    let op: '=' | '>' | '<' = '=';
    if (cond.includes('=')) op = '=';
    else if (cond.includes('>')) op = '>';
    else if (cond.includes('<')) op = '<';

    const sideParts = cond.split(op);
    const column = sideParts[0].trim();
    let value: any = sideParts[1].trim().replace(/['"]/g, '');
    if (!isNaN(Number(value))) {
      value = Number(value);
    }
    return { column, op, value };
  }
}
