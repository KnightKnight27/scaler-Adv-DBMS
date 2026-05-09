import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.sql.Statement;
import java.sql.SQLException;
import java.util.Random;

public class DatabaseExperiment {
    private static final String URL = "jdbc:sqlite:sample_java.db";
    private static final int TOTAL_RECORDS = 1000000;
    private static final int BATCH_SIZE = 100000;
    private static final Random random = new Random();

    public static void main(String[] args) {
        System.out.println("Starting SQLite Java Experiment...");

        try (Connection conn = DriverManager.getConnection(URL)) {
            // Create Table
            try (Statement stmt = conn.createStatement()) {
                stmt.execute("DROP TABLE IF EXISTS users");
                stmt.execute("CREATE TABLE users (" +
                        "id INTEGER PRIMARY KEY AUTOINCREMENT, " +
                        "username TEXT, " +
                        "email TEXT, " +
                        "age INTEGER)");
            }

            conn.setAutoCommit(false); // Enable manual transaction control for batch processing

            // Insert Data
            String insertSQL = "INSERT INTO users (username, email, age) VALUES (?, ?, ?)";
            try (PreparedStatement pstmt = conn.prepareStatement(insertSQL)) {
                System.out.println("Inserting " + TOTAL_RECORDS + " records in batches of " + BATCH_SIZE + "...");
                long startTime = System.currentTimeMillis();

                for (int i = 1; i <= TOTAL_RECORDS; i++) {
                    pstmt.setString(1, generateString(8));
                    pstmt.setString(2, generateString(12) + "@example.com");
                    pstmt.setInt(3, 18 + random.nextInt(82)); // Age between 18 and 99
                    pstmt.addBatch();

                    if (i % BATCH_SIZE == 0) {
                        pstmt.executeBatch();
                        conn.commit();
                        System.out.println("Inserted " + i + " records.");
                    }
                }
                long endTime = System.currentTimeMillis();
                System.out.println("Insertion completed in " + (endTime - startTime) + " ms.");
            }
            
            // Query Timing
            System.out.println("Executing SELECT * FROM users...");
            long selectStartTime = System.currentTimeMillis();
            try (Statement selectStmt = conn.createStatement();
                 java.sql.ResultSet rs = selectStmt.executeQuery("SELECT * FROM users")) {
                int count = 0;
                while (rs.next()) {
                    count++;
                }
                long selectEndTime = System.currentTimeMillis();
                System.out.println("Fetched " + count + " records in " + (selectEndTime - selectStartTime) + " ms.");
            }
            
            System.out.println("Database sample_java.db generated successfully.");

        } catch (SQLException e) {
            System.out.println("Database Error: " + e.getMessage());
            e.printStackTrace();
        }
    }

    private static String generateString(int length) {
        String chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        StringBuilder sb = new StringBuilder(length);
        for (int i = 0; i < length; i++) {
            sb.append(chars.charAt(random.nextInt(chars.length())));
        }
        return sb.toString();
    }
}
