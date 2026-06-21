package com.example.minidb.sql.executor;

import com.example.minidb.index.IndexManager;
import com.example.minidb.optimizer.Optimizer;
import com.example.minidb.optimizer.QueryPlan;
import com.example.minidb.sql.ast.*;
import com.example.minidb.storage.*;

import java.io.IOException;

public class Executor {

    private final TableStorage storage;
    private final IndexManager indexManager;
    private final Optimizer optimizer;

    public Executor(TableStorage storage,
                IndexManager indexManager,
                Optimizer optimizer) {

    this.storage = storage;
    this.indexManager = indexManager;
    this.optimizer = optimizer;
}

    public Object execute(
            Statement stmt)
            throws IOException {

        if (stmt instanceof InsertStatement insert) {

            Row row =
                    new Row(
                            insert.getValues()
                    );

            storage.insert(row);

            int primaryKey =
                    Integer.parseInt(
                            insert.getValues().get(0)
                    );

            int pageId =
                    storage.getLastInsertedPageId();

            indexManager.insert(
                    primaryKey,
                    pageId
            );

            return "INSERTED";
        }

        if (stmt instanceof SelectStatement select) {

    if (select.getWhereColumn() == null) {
        return storage.selectAll();
    }

    int key =
            Integer.parseInt(select.getWhereValue());

    QueryPlan plan =
            optimizer.choosePlan(
                    true,
                    storage.getPageCount(),
                    key
            );

    if (plan.getType() == QueryPlan.Type.INDEX_SCAN) {

        Integer pageId =
                indexManager.lookup(key);

        return storage.getRowByPageId(pageId);
    }

    return storage.selectAll();
}
   if (stmt instanceof DeleteStatement delete) {

    boolean removed =
            storage.deleteByPrimaryKey(
                    delete.getWhereValue()
            );

    return removed
            ? "DELETED"
            : "NOT FOUND";
}
        throw new RuntimeException(
                "Unsupported statement"
        );
    }
 

}