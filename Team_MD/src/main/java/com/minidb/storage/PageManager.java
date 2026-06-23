package com.minidb.storage;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;

/**
 * Manages reading/writing fixed-size pages to a single disk file.
 */
public class PageManager implements AutoCloseable {
    private final RandomAccessFile file;

    public PageManager(String filePath) throws IOException {
        File f = new File(filePath);
        this.file = new RandomAccessFile(f, "rw");
    }

    /**
     * Extends the file by one page and returns a new empty Page.
     */
    public Page allocatePage() throws IOException {
        int pageId = getNumPages();
        Page newPage = new Page(pageId);
        writePage(newPage);
        return newPage;
    }

    public Page readPage(int pageId) throws IOException {
        if (pageId < 0 || pageId >= getNumPages()) {
            throw new IllegalArgumentException("Invalid pageId: " + pageId);
        }
        byte[] buffer = new byte[Page.PAGE_SIZE];
        file.seek((long) pageId * Page.PAGE_SIZE);
        file.readFully(buffer);
        return Page.fromBytes(buffer);
    }

    public void writePage(Page page) throws IOException {
        file.seek((long) page.getPageId() * Page.PAGE_SIZE);
        file.write(page.getRawBytes());
        // Flush/force to disk
        file.getFD().sync();
    }

    public int getNumPages() throws IOException {
        return (int) (file.length() / Page.PAGE_SIZE);
    }

    @Override
    public void close() throws IOException {
        file.close();
    }
}
