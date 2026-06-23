package com.minidb.recovery;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;

public class WALManager implements AutoCloseable {
    private final RandomAccessFile file;
    private long nextLSN;

    public WALManager(String logFilePath) throws IOException {
        File f = new File(logFilePath);
        this.file = new RandomAccessFile(f, "rw");
        
        // Seek to end to start appending
        this.file.seek(this.file.length());
        
        // Next LSN could be initialized from the file size or a dedicated counter
        this.nextLSN = System.currentTimeMillis(); 
    }

    private boolean fsyncEnabled = true;

    public void setFsyncEnabled(boolean fsyncEnabled) {
        this.fsyncEnabled = fsyncEnabled;
    }

    public synchronized long appendLogRecord(LogRecord record) throws IOException {
        record.setLsn(nextLSN++);
        byte[] data = record.serialize();
        
        // Format: [length: 4 bytes][data...]
        ByteBuffer buffer = ByteBuffer.allocate(4 + data.length);
        buffer.putInt(data.length);
        buffer.put(data);
        
        file.write(buffer.array());
        
        // Force sync to disk immediately if enabled
        if (fsyncEnabled) {
            file.getFD().sync();
        }
        
        return record.getLsn();
    }
    
    /**
     * Reads all log records from the beginning of the file sequentially.
     */
    public List<LogRecord> readAllRecords() throws IOException {
        List<LogRecord> records = new ArrayList<>();
        long originalPos = file.getFilePointer();
        try {
            file.seek(0);
            while (file.getFilePointer() < file.length()) {
                int length = file.readInt();
                byte[] data = new byte[length];
                file.readFully(data);
                records.add(LogRecord.deserialize(data));
            }
        } finally {
            file.seek(originalPos);
        }
        return records;
    }

    @Override
    public void close() throws IOException {
        file.close();
    }
}
