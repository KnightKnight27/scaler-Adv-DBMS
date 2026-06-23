package minidb.storage;

import java.io.*;

/**
 * DiskManager performs raw, page-aligned reads and writes against a single
 * heap file on disk. It knows nothing about tuples or schemas — it just moves
 * PAGE_SIZE-byte blocks between memory and the file at offset (pageId * PAGE_SIZE).
 *
 * This is the lowest level of the storage engine.
 */
public final class DiskManager implements Closeable {
    private final RandomAccessFile file;
    private int numPages;

    public DiskManager(String path) throws IOException {
        File f = new File(path);
        boolean existed = f.exists();
        this.file = new RandomAccessFile(f, "rwd"); // "rwd" => writes go to disk (durability)
        this.numPages = existed ? (int) (file.length() / Page.PAGE_SIZE) : 0;
    }

    public synchronized int allocatePage() {
        return numPages++; // page ids are dense and monotonic
    }

    public int numPages() { return numPages; }

    public synchronized byte[] readPage(int pageId) throws IOException {
        byte[] buf = new byte[Page.PAGE_SIZE];
        long offset = (long) pageId * Page.PAGE_SIZE;
        if (offset >= file.length()) return buf; // never-written page => zeros
        file.seek(offset);
        file.readFully(buf);
        return buf;
    }

    public synchronized void writePage(int pageId, byte[] data) throws IOException {
        long offset = (long) pageId * Page.PAGE_SIZE;
        file.seek(offset);
        file.write(data, 0, Page.PAGE_SIZE);
    }

    @Override public synchronized void close() throws IOException { file.close(); }
}
