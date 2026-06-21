package com.example.minidb.storage;

public class Page {

    public static final int PAGE_SIZE = 4096;

    private final int pageId;

    private byte[] data;

    private boolean dirty;

    public Page(int pageId) {
        this.pageId = pageId;
        this.data = new byte[PAGE_SIZE];
        this.dirty = false;
    }

    public int getPageId() {
        return pageId;
    }

    public byte[] getData() {
        return data;
    }

    public void setData(byte[] data) {

        if (data.length > PAGE_SIZE) {
            throw new IllegalArgumentException(
                "Page size exceeded"
            );
        }

        System.arraycopy(
                data,
                0,
                this.data,
                0,
                data.length
        );

        dirty = true;
    }

    public boolean isDirty() {
        return dirty;
    }

    public void setDirty(boolean dirty) {
        this.dirty = dirty;
    }
}