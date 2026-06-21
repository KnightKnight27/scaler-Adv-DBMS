package com.example.minidb.storage;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

public class TableStorage {

    private final HeapFile heapFile;
    
    private final List<Integer> pages;
    
    private int lastInsertedPageId;

    public TableStorage(
            HeapFile heapFile) {

        this.heapFile = heapFile;
        this.pages = new ArrayList<>();
    }

    public void insert(
            Row row)
            throws IOException {

        int pageId =
                heapFile.insertRecord(
                        row.toString()
                );

        pages.add(pageId);
        lastInsertedPageId = pageId;
    }

    public List<Row> selectAll()
            throws IOException {

        List<Row> result =
                new ArrayList<>();

        for (Integer pageId : pages) {

            List<String> records =
                    heapFile.readAllRecords(
                            pageId
                    );

            for (String record : records) {

                List<String> values =
                        List.of(
                                record.split(",")
                        );

                result.add(
                        new Row(values)
                );
            }
        }

        return result;
    }

    public Row getRowByPageId(
        int pageId)
        throws IOException {

    String record =
            heapFile.readAllRecords(pageId).get(0);

    return new Row(
            java.util.List.of(
                    record.split(",")
            )
    );
}

public int getLastInsertedPageId() {
    return lastInsertedPageId;
}

public int getPageCount() {
    return pages.size();
}

public boolean deleteByPrimaryKey(
        String key)
        throws IOException {

    List<Row> rows =
            selectAll();

    pages.clear();

    boolean deleted = false;

    for (Row row : rows) {

        if (!row.getValues()
                .get(0)
                .equals(key)) {

            insert(row);

        } else {

            deleted = true;
        }
    }

    return deleted;
}

}