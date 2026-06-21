package com.example.minidb.sql;

import com.example.minidb.sql.ast.Statement;
import com.example.minidb.sql.executor.Executor;
import com.example.minidb.storage.*;

import com.example.minidb.index.IndexManager;
import com.example.minidb.optimizer.Optimizer;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.*;

public class ExecutorIntegrationTest {

    @Test
    void testInsertAndSelect()
            throws Exception {

        PageManager pm = new PageManager(
                "database/integration.db");

        HeapFile heap = new HeapFile(pm);

        TableStorage storage = new TableStorage(heap);

        IndexManager indexManager = new IndexManager();
        Optimizer optimizer = new Optimizer(indexManager);

        Executor executor = new Executor(
                storage,
                indexManager,
                optimizer);

        Tokenizer tokenizer = new Tokenizer();

        Parser parser = new Parser();

        Statement insert = parser.parse(
                tokenizer.tokenize(
                        "INSERT INTO users VALUES (1,Alice);"));

        executor.execute(insert);

        Statement select = parser.parse(
                tokenizer.tokenize(
                        "SELECT * FROM users;"));

        Object result = executor.execute(select);

        List<?> rows = (List<?>) result;

        assertEquals(
                1,
                rows.size());
    }
}