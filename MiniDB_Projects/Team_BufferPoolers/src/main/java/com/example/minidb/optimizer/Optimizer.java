package com.example.minidb.optimizer;

import com.example.minidb.index.IndexManager;

public class Optimizer {

    private final IndexManager indexManager;
    private final CostModel costModel;

    public Optimizer(IndexManager indexManager) {
        this.indexManager = indexManager;
        this.costModel = new CostModel();
    }

    public QueryPlan choosePlan(boolean hasWhere, int tablePages, int key) {

        if (!hasWhere) {
            return new QueryPlan(QueryPlan.Type.TABLE_SCAN);
        }

        Integer pageId = indexManager.lookup(key);

        if (pageId == null) {
            return new QueryPlan(QueryPlan.Type.TABLE_SCAN);
        }

        double tableCost = costModel.tableScanCost(tablePages);
        double indexCost = costModel.indexScanCost(2); // assume small tree

        if (indexCost < tableCost) {
            return new QueryPlan(QueryPlan.Type.INDEX_SCAN);
        }

        return new QueryPlan(QueryPlan.Type.TABLE_SCAN);
    }

    public QueryPlan choosePlan(
        boolean hasWhere,
        int tablePages,
        int key,
        int matchingRows,
        int totalRows) {

    if (!hasWhere) {
        return new QueryPlan(
                QueryPlan.Type.TABLE_SCAN
        );
    }

    double selectivity =
            costModel.estimateSelectivity(
                    matchingRows,
                    totalRows
            );

    Integer pageId =
            indexManager.lookup(key);

    if (pageId == null) {
        return new QueryPlan(
                QueryPlan.Type.TABLE_SCAN
        );
    }

    if (selectivity < 0.3) {
        return new QueryPlan(
                QueryPlan.Type.INDEX_SCAN
        );
    }

    return new QueryPlan(
            QueryPlan.Type.TABLE_SCAN
    );
}
public String chooseJoinOrder(
        int rowsA,
        int rowsB) {

    if (rowsA <= rowsB) {
        return "A_JOIN_B";
    }

    return "B_JOIN_A";
}
public String chooseJoinOrder(
        int rowsA,
        int rowsB,
        int rowsC) {

    int smallest =
            Math.min(rowsA,
                    Math.min(rowsB, rowsC));

    if (smallest == rowsA) {
        return "(A JOIN B) JOIN C";
    }

    if (smallest == rowsB) {
        return "(B JOIN A) JOIN C";
    }

    return "(C JOIN A) JOIN B";
}
}
