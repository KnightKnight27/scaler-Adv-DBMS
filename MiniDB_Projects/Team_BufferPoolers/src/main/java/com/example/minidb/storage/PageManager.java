package com.example.minidb.storage;

import java.io.IOException;
import java.io.RandomAccessFile;

public class PageManager {

    private final String fileName;

    public PageManager(String fileName) {
        this.fileName = fileName;
    }

    public int allocatePage() throws IOException {

        try (RandomAccessFile file =
                     new RandomAccessFile(fileName, "rw")) {

            long length = file.length();

            int pageId =
                    (int)(length / Page.PAGE_SIZE);

            file.setLength(
                    length + Page.PAGE_SIZE
            );

            return pageId;
        }
    }

    public void writePage(Page page)
            throws IOException {

        try (RandomAccessFile file =
                     new RandomAccessFile(fileName, "rw")) {

            long offset =
                    (long) page.getPageId()
                            * Page.PAGE_SIZE;

            file.seek(offset);

            file.write(page.getData());

            page.setDirty(false);
        }
    }

    public Page readPage(int pageId)
            throws IOException {

        try (RandomAccessFile file =
                     new RandomAccessFile(fileName, "rw")) {

            long offset =
                    (long) pageId
                            * Page.PAGE_SIZE;

            file.seek(offset);

            Page page =
                    new Page(pageId);

            file.read(page.getData());

            return page;
        }
    }
}