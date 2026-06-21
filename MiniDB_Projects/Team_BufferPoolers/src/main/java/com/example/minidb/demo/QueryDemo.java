
package com.example.minidb.demo;

import com.example.minidb.index.IndexManager;
import com.example.minidb.optimizer.Optimizer;
import com.example.minidb.sql.Parser;
import com.example.minidb.sql.Tokenizer;
import com.example.minidb.sql.ast.Statement;
import com.example.minidb.sql.executor.Executor;
import com.example.minidb.storage.HeapFile;
import com.example.minidb.storage.PageManager;
import com.example.minidb.storage.TableStorage;

public class QueryDemo {

    public static void main(String[] args)
            throws Exception {

        System.out.println("=================================");
        System.out.println("MiniDB Query Execution Demo");
        System.out.println("=================================");

        PageManager pm =
                new PageManager(
                        "database/query_demo.db"
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

        String[] queries = {

    "INSERT INTO users VALUES (1,Alice);",

    "INSERT INTO users VALUES (2,Bob);",

    "INSERT INTO users VALUES (3,Charlie);",

    "SELECT * FROM users;",

    "SELECT * FROM users WHERE id = 2;",
    "DELETE FROM users WHERE id = 2;",
"SELECT * FROM users;"
};

        for (String sql : queries) {

            System.out.println();
            System.out.println("SQL > " + sql);

            Statement stmt =
                    parser.parse(
                            tokenizer.tokenize(sql)
                    );

            Object result =
                    executor.execute(stmt);

            System.out.println(
                    "Result: " + result
            );
        }

        System.out.println();
        System.out.println("=================================");
        System.out.println("Demo Complete");
        System.out.println("=================================");
    }
}

