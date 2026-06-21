package com.example.minidb.optimizer;

public class CostModel {

    public double tableScanCost(int numPages) {
        return numPages * 1.0;
    }

    public double indexScanCost(int treeHeight) {
        return treeHeight * 0.2 + 1;
    }

    public double estimateSelectivity(
        int matchingRows,
        int totalRows) {

    if (totalRows == 0) {
        return 1.0;
    }

    return (double) matchingRows / totalRows;
}

}