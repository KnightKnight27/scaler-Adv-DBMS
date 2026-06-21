package com.example.minidb.transaction;

public class Transaction {

    private final long transactionId;

    public Transaction(long transactionId) {
        this.transactionId = transactionId;
    }

    public long getTransactionId() {
        return transactionId;
    }
}
