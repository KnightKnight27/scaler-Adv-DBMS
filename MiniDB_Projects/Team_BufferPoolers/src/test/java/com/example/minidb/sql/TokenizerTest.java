package com.example.minidb.sql;

import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.*;

public class TokenizerTest {

    @Test
    void testTokenizer() {

        Tokenizer tokenizer =
                new Tokenizer();

        List<Token> tokens =
                tokenizer.tokenize(
                        "SELECT * FROM users;"
                );

        assertFalse(
                tokens.isEmpty()
        );

        assertEquals(
                "SELECT",
                tokens.get(0).value()
        );
    }
}