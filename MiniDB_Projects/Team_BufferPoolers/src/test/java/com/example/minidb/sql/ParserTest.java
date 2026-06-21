package com.example.minidb.sql;

import com.example.minidb.sql.ast.InsertStatement;
import com.example.minidb.sql.ast.SelectStatement;
import com.example.minidb.sql.ast.Statement;

import org.junit.jupiter.api.Test;


import static org.junit.jupiter.api.Assertions.*;

public class ParserTest {

    @Test
    void testSelect() {

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

        assertTrue(
                stmt instanceof SelectStatement
        );
    }

    @Test
    void testInsert() {

        Tokenizer tokenizer =
                new Tokenizer();

        Parser parser =
                new Parser();

        Statement stmt =
                parser.parse(
                        tokenizer.tokenize(
                                "INSERT INTO users VALUES (1,Alice);"
                        )
                );

        assertTrue(
                stmt instanceof InsertStatement
        );
    }
}
