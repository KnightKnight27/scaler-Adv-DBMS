package com.minidb;

import net.sf.jsqlparser.parser.CCJSqlParserUtil;
import net.sf.jsqlparser.statement.Statement;

public class Main {
    public static void main(String[] args) {
        String sql = "SELECT name FROM users WHERE id = 5";
        try {
            Statement statement = CCJSqlParserUtil.parse(sql);
            System.out.println("Parsed statement: " + statement);
        } catch (Exception e) {
            System.err.println("Error parsing SQL: " + e.getMessage());
            e.printStackTrace();
        }
    }
}
