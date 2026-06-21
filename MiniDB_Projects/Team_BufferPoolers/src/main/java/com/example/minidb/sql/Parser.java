package com.example.minidb.sql;

import com.example.minidb.sql.ast.*;

import java.util.ArrayList;
import java.util.List;

public class Parser {

    public Statement parse(
            List<Token> tokens) {

        String first = tokens.get(0)
                .value();

        switch (first) {

            case "SELECT":
                return parseSelect(tokens);

            case "INSERT":
                return parseInsert(tokens);

            case "DELETE":
                return parseDelete(tokens);

            default:
                throw new RuntimeException(
                        "Unsupported SQL");
        }
    }

    private Statement parseSelect(
            List<Token> tokens) {

        String tableName = tokens.get(3).value();

        String whereColumn = null;
        String whereValue = null;

        for (int i = 0; i < tokens.size(); i++) {

            if ("WHERE".equals(
                    tokens.get(i).value())) {

                whereColumn = tokens.get(i + 1).value();

                whereValue = tokens.get(i + 3).value();
            }
        }

        return new SelectStatement(
                tableName,
                whereColumn,
                whereValue);
    }

    private Statement parseDelete(
        List<Token> tokens) {

    String tableName =
            tokens.get(2).value();

    String whereColumn = null;
    String whereValue = null;

    for (int i = 0; i < tokens.size(); i++) {

        if ("WHERE".equals(
                tokens.get(i).value())) {

            whereColumn =
                    tokens.get(i + 1).value();

            whereValue =
                    tokens.get(i + 3).value();
        }
    }

    return new DeleteStatement(
            tableName,
            whereColumn,
            whereValue
    );
}

    private Statement parseInsert(
            List<Token> tokens) {

        String tableName = tokens.get(2).value();

        List<String> values = new ArrayList<>();

        for (Token token : tokens) {

            if (token.type() == TokenType.NUMBER
                    || token.type() == TokenType.IDENTIFIER) {

                values.add(
                        token.value());
            }
        }

        if (!values.isEmpty()) {
            values.remove(0);
        }

        return new InsertStatement(
                tableName,
                values);
    }
}
