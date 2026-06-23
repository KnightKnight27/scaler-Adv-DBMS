package com.minidb.query.operators;

import com.minidb.query.Operator;
import com.minidb.storage.Row;

import java.util.ArrayList;
import java.util.List;

public class JoinOperator implements Operator {
    private Operator left;
    private Operator right;
    private int leftJoinColumnIndex;
    private int rightJoinColumnIndex;
    private Row currentLeftRow;

    public JoinOperator(Operator left, Operator right, int leftJoinColumnIndex, int rightJoinColumnIndex) {
        this.left = left;
        this.right = right;
        this.leftJoinColumnIndex = leftJoinColumnIndex;
        this.rightJoinColumnIndex = rightJoinColumnIndex;
    }

    @Override
    public void open() throws Exception {
        left.open();
        right.open();
        currentLeftRow = left.next();
    }

    @Override
    public Row next() throws Exception {
        if (currentLeftRow == null) return null;

        while (currentLeftRow != null) {
            Row rightRow = right.next();
            if (rightRow != null) {
                if (currentLeftRow.getValue(leftJoinColumnIndex).equals(rightRow.getValue(rightJoinColumnIndex))) {
                    return mergeRows(currentLeftRow, rightRow);
                }
            } else {
                right.close();
                right.open();
                currentLeftRow = left.next();
            }
        }
        return null;
    }

    private Row mergeRows(Row r1, Row r2) {
        List<Object> merged = new ArrayList<>();
        for (Object v : r1.getValues()) merged.add(v);
        for (Object v : r2.getValues()) merged.add(v);
        return new Row(merged);
    }

    @Override
    public void close() throws Exception {
        left.close();
        right.close();
    }
}
