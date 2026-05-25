package com.scaler.adbms.lab3;

import java.nio.charset.StandardCharsets;
import java.util.Arrays;

/**
 * A single in-memory page slot. Holds raw bytes plus the bookkeeping the
 * buffer pool needs: page id, pin count, dirty flag.
 *
 * <p>Pin/dirty mutation methods are package-private on purpose — only the
 * {@link BufferPoolManager} should change those, never callers.
 */
public final class Page {
    public static final int PAGE_SIZE = 4096;
    public static final int INVALID_PAGE_ID = -1;

    private int pageId;
    private int pinCount;
    private boolean dirty;
    private final byte[] data = new byte[PAGE_SIZE];

    public Page() {
        reset(INVALID_PAGE_ID);
    }

    public int getPageId() {
        return pageId;
    }

    public int getPinCount() {
        return pinCount;
    }

    public boolean isDirty() {
        return dirty;
    }

    public byte[] getData() {
        return data;
    }

    public void reset(int newPageId) {
        this.pageId = newPageId;
        this.pinCount = 0;
        this.dirty = false;
        Arrays.fill(data, (byte) 0);
    }

    public void write(int offset, byte[] src) {
        if (offset < 0 || offset + src.length > PAGE_SIZE) {
            throw new IndexOutOfBoundsException(
                "write out of bounds: offset=" + offset + ", len=" + src.length);
        }
        System.arraycopy(src, 0, data, offset, src.length);
        dirty = true;
    }

    public byte[] read(int offset, int length) {
        if (offset < 0 || offset + length > PAGE_SIZE) {
            throw new IndexOutOfBoundsException(
                "read out of bounds: offset=" + offset + ", len=" + length);
        }
        byte[] out = new byte[length];
        System.arraycopy(data, offset, out, 0, length);
        return out;
    }

    public void writeString(String s) {
        byte[] bytes = s.getBytes(StandardCharsets.UTF_8);
        if (bytes.length + 1 > PAGE_SIZE) {
            throw new IllegalArgumentException("string too large for a page");
        }
        write(0, bytes);
        data[bytes.length] = 0;
    }

    public String readString() {
        int end = 0;
        while (end < PAGE_SIZE && data[end] != 0) end++;
        return new String(data, 0, end, StandardCharsets.UTF_8);
    }

    // --- package-private hooks for BufferPoolManager ----------------------

    void incrPin() {
        pinCount++;
    }

    void decrPin() {
        if (pinCount <= 0) {
            throw new IllegalStateException("pin count underflow on page " + pageId);
        }
        pinCount--;
    }

    void markDirty(boolean isDirty) {
        this.dirty = this.dirty || isDirty;
    }

    void clearDirty() {
        this.dirty = false;
    }

    @Override
    public String toString() {
        return "Page{id=" + pageId + ", pin=" + pinCount + ", dirty=" + dirty + "}";
    }
}
