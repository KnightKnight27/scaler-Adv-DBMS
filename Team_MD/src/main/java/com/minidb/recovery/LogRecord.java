package com.minidb.recovery;

import java.nio.ByteBuffer;

public class LogRecord {
    public enum Type {
        INSERT, UPDATE, DELETE, COMMIT, ABORT
    }

    private final long txnId;
    private final int pageId;
    private final int slotNumber;   // slot within the page (-1 for COMMIT/ABORT)
    private final byte[] beforeImage;
    private final byte[] afterImage;
    private final Type type;
    private long lsn;

    public LogRecord(long txnId, int pageId, int slotNumber,
                     byte[] beforeImage, byte[] afterImage, Type type) {
        this.txnId = txnId;
        this.pageId = pageId;
        this.slotNumber = slotNumber;
        this.beforeImage = beforeImage;
        this.afterImage = afterImage;
        this.type = type;
        this.lsn = -1;
    }

    /** Legacy constructor for COMMIT/ABORT (no page or slot). */
    public LogRecord(long txnId, int pageId, byte[] beforeImage, byte[] afterImage, Type type) {
        this(txnId, pageId, -1, beforeImage, afterImage, type);
    }

    public long getTxnId()       { return txnId; }
    public int  getPageId()      { return pageId; }
    public int  getSlotNumber()  { return slotNumber; }
    public byte[] getBeforeImage() { return beforeImage; }
    public byte[] getAfterImage()  { return afterImage; }
    public Type getType()        { return type; }
    public long getLsn()         { return lsn; }
    public void setLsn(long lsn) { this.lsn = lsn; }

    public byte[] serialize() {
        int beforeLen = beforeImage != null ? beforeImage.length : 0;
        int afterLen  = afterImage  != null ? afterImage.length  : 0;

        // type(4) + lsn(8) + txnId(8) + pageId(4) + slotNumber(4)
        // + beforeLen(4) + before + afterLen(4) + after
        int size = 4 + 8 + 8 + 4 + 4 + 4 + beforeLen + 4 + afterLen;
        ByteBuffer buf = ByteBuffer.allocate(size);

        buf.putInt(type.ordinal());
        buf.putLong(lsn);
        buf.putLong(txnId);
        buf.putInt(pageId);
        buf.putInt(slotNumber);

        buf.putInt(beforeLen);
        if (beforeLen > 0) buf.put(beforeImage);

        buf.putInt(afterLen);
        if (afterLen > 0) buf.put(afterImage);

        return buf.array();
    }

    public static LogRecord deserialize(byte[] data) {
        ByteBuffer buf = ByteBuffer.wrap(data);
        Type type       = Type.values()[buf.getInt()];
        long lsn        = buf.getLong();
        long txnId      = buf.getLong();
        int  pageId     = buf.getInt();
        int  slotNumber = buf.getInt();

        int beforeLen = buf.getInt();
        byte[] beforeImage = null;
        if (beforeLen > 0) {
            beforeImage = new byte[beforeLen];
            buf.get(beforeImage);
        }

        int afterLen = buf.getInt();
        byte[] afterImage = null;
        if (afterLen > 0) {
            afterImage = new byte[afterLen];
            buf.get(afterImage);
        }

        LogRecord rec = new LogRecord(txnId, pageId, slotNumber, beforeImage, afterImage, type);
        rec.setLsn(lsn);
        return rec;
    }
}
