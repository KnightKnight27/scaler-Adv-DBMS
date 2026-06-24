package com.scaler.adbms.lab3;

import java.io.IOException;
import java.io.RandomAccessFile;
import java.io.UncheckedIOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.Map;

/**
 * Tiny disk simulator. Each page lives at file offset {@code page_id * PAGE_SIZE}.
 * Can run in-memory (no path) or backed by a regular file.
 */
public final class DiskManager implements AutoCloseable {
    private final boolean inMemory;
    private final RandomAccessFile fh;     // null in memory mode
    private final Map<Integer, byte[]> mem; // null in file mode
    private int nextPageId = 0;
    private final Object lock = new Object();

    /** In-memory mode. */
    public DiskManager() {
        this.inMemory = true;
        this.fh = null;
        this.mem = new HashMap<>();
    }

    /** File-backed mode. Creates the file if it does not exist. */
    public DiskManager(Path dbPath) {
        this.inMemory = false;
        this.mem = null;
        try {
            if (!Files.exists(dbPath)) {
                if (dbPath.getParent() != null) {
                    Files.createDirectories(dbPath.getParent());
                }
                Files.createFile(dbPath);
            }
            this.fh = new RandomAccessFile(dbPath.toFile(), "rw");
            long size = this.fh.length();
            this.nextPageId = (int) (size / Page.PAGE_SIZE);
        } catch (IOException e) {
            throw new UncheckedIOException("failed to open " + dbPath, e);
        }
    }

    public int allocatePage() {
        synchronized (lock) {
            int pid = nextPageId++;
            byte[] zeros = new byte[Page.PAGE_SIZE];
            if (inMemory) {
                mem.put(pid, zeros);
            } else {
                try {
                    fh.seek((long) pid * Page.PAGE_SIZE);
                    fh.write(zeros);
                } catch (IOException e) {
                    throw new UncheckedIOException(e);
                }
            }
            return pid;
        }
    }

    public void deallocatePage(int pageId) {
        synchronized (lock) {
            if (inMemory) {
                mem.remove(pageId);
                return;
            }
            try {
                fh.seek((long) pageId * Page.PAGE_SIZE);
                fh.write(new byte[Page.PAGE_SIZE]);
            } catch (IOException e) {
                throw new UncheckedIOException(e);
            }
        }
    }

    public void readPage(int pageId, byte[] dst) {
        if (dst.length != Page.PAGE_SIZE) {
            throw new IllegalArgumentException("dst must be PAGE_SIZE bytes");
        }
        synchronized (lock) {
            if (inMemory) {
                byte[] src = mem.get(pageId);
                if (src == null) {
                    throw new IllegalArgumentException("page " + pageId + " not allocated");
                }
                System.arraycopy(src, 0, dst, 0, Page.PAGE_SIZE);
                return;
            }
            try {
                fh.seek((long) pageId * Page.PAGE_SIZE);
                fh.readFully(dst);
            } catch (IOException e) {
                throw new UncheckedIOException(e);
            }
        }
    }

    public void writePage(int pageId, byte[] src) {
        if (src.length != Page.PAGE_SIZE) {
            throw new IllegalArgumentException("src must be PAGE_SIZE bytes");
        }
        synchronized (lock) {
            if (inMemory) {
                byte[] copy = new byte[Page.PAGE_SIZE];
                System.arraycopy(src, 0, copy, 0, Page.PAGE_SIZE);
                mem.put(pageId, copy);
                return;
            }
            try {
                fh.seek((long) pageId * Page.PAGE_SIZE);
                fh.write(src);
            } catch (IOException e) {
                throw new UncheckedIOException(e);
            }
        }
    }

    public int nextPageId() {
        synchronized (lock) {
            return nextPageId;
        }
    }

    @Override
    public void close() {
        synchronized (lock) {
            if (fh != null) {
                try {
                    fh.close();
                } catch (IOException e) {
                    throw new UncheckedIOException(e);
                }
            }
        }
    }
}
