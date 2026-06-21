package com.example.minidb.sql;

import com.example.minidb.index.IndexManager;
import com.example.minidb.sql.ast.Statement;
import com.example.minidb.sql.executor.Executor;
import com.example.minidb.storage.*;
import com.example.minidb.optimizer.Optimizer;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

public class ExecutorTest {

    @Test
    void testExecuteSelect() throws Exception {

        PageManager pm =
                new PageManager(
                        "database/executor_test.db"
                );

        HeapFile heap =
                new HeapFile(pm);

        TableStorage storage =
                new TableStorage(heap);

        IndexManager indexManager =
                new IndexManager();
        
                Optimizer optimizer =
                new Optimizer(indexManager);
        Executor executor =
                new Executor(
                        storage,
                        indexManager,
                        optimizer
                );

        Tokenizer tokenizer =
                new Tokenizer();

        Parser parser =
                new Parser();

        Statement stmt =
                parser.parse(
                        tokenizer.tokenize(
                                "SELECT * FROM users;"
                        )
                );

        Object result =
                executor.execute(stmt);

        assertNotNull(result);
    }
}