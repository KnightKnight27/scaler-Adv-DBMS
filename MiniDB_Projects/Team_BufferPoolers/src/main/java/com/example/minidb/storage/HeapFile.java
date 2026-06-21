package com.example.minidb.storage;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

public class HeapFile {

   

    private final PageManager pageManager;

    public HeapFile(PageManager pageManager) {
        this.pageManager = pageManager;
    }

    public int insertRecord(String record)
            throws IOException {

        byte[] recordBytes =
                record.getBytes(StandardCharsets.UTF_8);

        int pageId =
                pageManager.allocatePage();

        Page page =
                new Page(pageId);

        byte[] data =
                page.getData();

        ByteBuffer buffer =
                ByteBuffer.wrap(data);

        buffer.putInt(1);

        buffer.putInt(recordBytes.length);

        buffer.put(recordBytes);

        pageManager.writePage(page);

        return pageId;
    }

    public List<String> readAllRecords(int pageId)
            throws IOException {

        Page page =
                pageManager.readPage(pageId);

        ByteBuffer buffer =
                ByteBuffer.wrap(page.getData());

        int recordCount =
                buffer.getInt();

        List<String> records =
                new ArrayList<>();

        for (int i = 0;
             i < recordCount;
             i++) {

            int len =
                    buffer.getInt();

            byte[] bytes =
                    new byte[len];

            buffer.get(bytes);

            records.add(
                    new String(
                            bytes,
                            StandardCharsets.UTF_8
                    )
            );
        }

        return records;
    }
}