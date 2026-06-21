package com.example.minidb.sql;

import java.util.ArrayList;
import java.util.List;

public class Tokenizer {

    public List<Token> tokenize(
            String sql) {

        List<Token> tokens =
                new ArrayList<>();

        String[] parts =
        sql.replace("(", " ( ")
           .replace(")", " ) ")
           .replace(",", " , ")
           .replace("=", " = ")
           .replace(";", " ; ")
           .split("\\s+");

        for (String part : parts) {

            if (part.isBlank()) {
                continue;
            }

            String upper =
                    part.toUpperCase();

            switch (upper) {

                case "SELECT":
                case "INSERT":
                case "INTO":
                case "VALUES":
                case "FROM":
                case "WHERE":
                case "DELETE":
                case "UPDATE":

                    tokens.add(
                            new Token(
                                    TokenType.KEYWORD,
                                    upper
                            )
                    );
                    break;

                default:

                    if (part.matches("\\d+")) {

                        tokens.add(
                                new Token(
                                        TokenType.NUMBER,
                                        part
                                )
                        );

                    } else if (
                            part.equals("(")
                            || part.equals(")")
                            || part.equals(",")
                            || part.equals(";")
                            || part.equals("=")
                    ) {

                        tokens.add(
                                new Token(
                                        TokenType.SYMBOL,
                                        part
                                )
                        );

                    } else {

                        tokens.add(
                                new Token(
                                        TokenType.IDENTIFIER,
                                        part
                                )
                        );
                    }
            }
        }

        return tokens;
    }
}