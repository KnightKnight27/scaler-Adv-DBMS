import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;

public class FileIOAssignment {
    private static final String FILE_NAME = "assignment-data.txt";

    public static void main(String[] args) {
        String content = "Advanced DBMS Lab 1\n"
                + "This file was written using FileOutputStream and read using FileInputStream.\n"
                + "It demonstrates how user code reaches the operating system through system calls.\n";

        try {
            writeFile(FILE_NAME, content);
            String readBack = readFile(FILE_NAME);

            System.out.println("File written successfully: " + new File(FILE_NAME).getAbsolutePath());
            System.out.println("\nRead back from file:\n");
            System.out.println(readBack);
        } catch (IOException e) {
            System.err.println("File I/O failed: " + e.getMessage());
            e.printStackTrace();
        }
    }

    private static void writeFile(String fileName, String data) throws IOException {
        try (FileOutputStream outputStream = new FileOutputStream(fileName)) {
            outputStream.write(data.getBytes(StandardCharsets.UTF_8));
            outputStream.flush();
        }
    }

    private static String readFile(String fileName) throws IOException {
        File file = new File(fileName);
        byte[] buffer = new byte[(int) file.length()];

        try (FileInputStream inputStream = new FileInputStream(file)) {
            int bytesRead = inputStream.read(buffer);
            if (bytesRead == -1) {
                return "";
            }
            return new String(buffer, 0, bytesRead, StandardCharsets.UTF_8);
        }
    }
}