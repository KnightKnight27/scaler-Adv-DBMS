package minidb.storage;

import minidb.common.Types.*;
import java.util.*;

/**
 * A fixed-size slotted page (4 KB). This is the unit of disk I/O and buffering.
 *
 * Layout (slotted-page design, the classic approach used by Postgres/SQLite):
 *
 *   [ header | slot directory ... ->        <- ... tuple data ]
 *
 *   Header (bytes):
 *     0..3   : pageId
 *     4..7   : slotCount         (number of slot entries)
 *     8..11  : freeSpacePointer  (offset where the next tuple will be written,
 *                                 growing downward from the end of the page)
 *
 *   Slot directory: each slot is 8 bytes = (offset:int, length:int).
 *     A length of -1 marks a deleted/empty slot (tombstone).
 *
 * Tuples are stored from the END of the page backwards; the slot directory
 * grows from the front. They meet in the middle; when they would overlap the
 * page is full.
 *
 * This page is materialized in memory as a byte[] so it can be flushed to disk
 * verbatim by the PageManager.
 */
public final class Page {
    public static final int PAGE_SIZE = 4096;
    private static final int HEADER_SIZE = 12;
    private static final int SLOT_SIZE = 8;

    public final byte[] data = new byte[PAGE_SIZE];
    public boolean dirty = false;
    public int pinCount = 0;

    private final Schema schema;

    public Page(int pageId, Schema schema) {
        this.schema = schema;
        setPageId(pageId);
        setSlotCount(0);
        setFreePointer(PAGE_SIZE);
    }

    /** Wrap an existing on-disk byte array. */
    public Page(byte[] raw, Schema schema) {
        System.arraycopy(raw, 0, this.data, 0, PAGE_SIZE);
        this.schema = schema;
    }

    // ---- header accessors ----
    public int getPageId()            { return readInt(0); }
    private void setPageId(int v)      { writeInt(0, v); }
    public int getSlotCount()         { return readInt(4); }
    private void setSlotCount(int v)   { writeInt(4, v); }
    private int getFreePointer()       { return readInt(8); }
    private void setFreePointer(int v) { writeInt(8, v); }

    private int slotOffsetPos(int slot) { return HEADER_SIZE + slot * SLOT_SIZE; }

    /**
     * Insert a tuple. Returns the slot number, or -1 if the page is full.
     */
    public int insert(Tuple t) {
        byte[] bytes = serialize(t);
        int slotCount = getSlotCount();
        int free = getFreePointer();

        // The tuple grows downward from `free`; the slot directory grows upward.
        // They must not overlap once we add this tuple AND its new slot entry.
        if (free - bytes.length < HEADER_SIZE + (slotCount + 1) * SLOT_SIZE) {
            return -1; // page full
        }

        int newOffset = free - bytes.length;
        System.arraycopy(bytes, 0, data, newOffset, bytes.length);

        int slot = slotCount;
        writeInt(slotOffsetPos(slot), newOffset);
        writeInt(slotOffsetPos(slot) + 4, bytes.length);

        setFreePointer(newOffset);
        setSlotCount(slotCount + 1);
        dirty = true;
        return slot;
    }

    /** Read a tuple by slot; returns null if the slot is a tombstone. */
    public Tuple read(int slot) {
        if (slot < 0 || slot >= getSlotCount()) return null;
        int offset = readInt(slotOffsetPos(slot));
        int length = readInt(slotOffsetPos(slot) + 4);
        if (length < 0) return null; // deleted
        byte[] bytes = Arrays.copyOfRange(data, offset, offset + length);
        Tuple t = deserialize(bytes);
        t.rid = new RID(getPageId(), slot);
        return t;
    }

    /** Mark a slot as deleted (tombstone). Space is not reclaimed (simple design). */
    public void delete(int slot) {
        if (slot < 0 || slot >= getSlotCount()) return;
        writeInt(slotOffsetPos(slot) + 4, -1);
        dirty = true;
    }

    /** Overwrite a slot in place if the new tuple fits in the old footprint. */
    public boolean update(int slot, Tuple t) {
        int length = readInt(slotOffsetPos(slot) + 4);
        if (length < 0) return false;
        byte[] bytes = serialize(t);
        if (bytes.length > length) return false; // doesn't fit in place
        int offset = readInt(slotOffsetPos(slot));
        System.arraycopy(bytes, 0, data, offset, bytes.length);
        writeInt(slotOffsetPos(slot) + 4, bytes.length);
        dirty = true;
        return true;
    }

    public List<Tuple> scan() {
        List<Tuple> out = new ArrayList<>();
        for (int s = 0; s < getSlotCount(); s++) {
            Tuple t = read(s);
            if (t != null) out.add(t);
        }
        return out;
    }

    // ---- tuple serialization ----
    // Format: for each column, INT -> 4 bytes; STRING -> 4-byte length + UTF-8 bytes.
    // A leading byte encodes MVCC flags + 16 bytes of begin/end timestamps so the
    // version metadata survives a disk round-trip.
    private byte[] serialize(Tuple t) {
        // estimate size
        int size = 1 + 16; // flags + beginTs + endTs
        for (int i = 0; i < schema.size(); i++) {
            if (schema.column(i).type == ColType.INT) size += 4;
            else {
                String s = (String) t.values[i];
                size += 4 + (s == null ? 0 : s.getBytes().length);
            }
        }
        byte[] b = new byte[size];
        int p = 0;
        b[p++] = (byte) (t.deleted ? 1 : 0);
        p = putLong(b, p, t.beginTs);
        p = putLong(b, p, t.endTs);
        for (int i = 0; i < schema.size(); i++) {
            if (schema.column(i).type == ColType.INT) {
                p = putInt(b, p, (Integer) t.values[i]);
            } else {
                byte[] s = ((String) t.values[i]).getBytes();
                p = putInt(b, p, s.length);
                System.arraycopy(s, 0, b, p, s.length);
                p += s.length;
            }
        }
        return b;
    }

    private Tuple deserialize(byte[] b) {
        int p = 0;
        boolean del = b[p++] == 1;
        long begin = getLong(b, p); p += 8;
        long end = getLong(b, p); p += 8;
        Object[] vals = new Object[schema.size()];
        for (int i = 0; i < schema.size(); i++) {
            if (schema.column(i).type == ColType.INT) {
                vals[i] = getInt(b, p); p += 4;
            } else {
                int len = getInt(b, p); p += 4;
                vals[i] = new String(b, p, len); p += len;
            }
        }
        Tuple t = new Tuple(vals);
        t.deleted = del; t.beginTs = begin; t.endTs = end;
        return t;
    }

    // ---- little binary helpers ----
    private void writeInt(int pos, int v) { putInt(data, pos, v); }
    private int readInt(int pos) { return getInt(data, pos); }

    private static int putInt(byte[] b, int p, int v) {
        b[p]   = (byte)(v >>> 24); b[p+1] = (byte)(v >>> 16);
        b[p+2] = (byte)(v >>> 8);  b[p+3] = (byte) v;
        return p + 4;
    }
    private static int getInt(byte[] b, int p) {
        return ((b[p]&0xff)<<24)|((b[p+1]&0xff)<<16)|((b[p+2]&0xff)<<8)|(b[p+3]&0xff);
    }
    private static int putLong(byte[] b, int p, long v) {
        for (int i = 7; i >= 0; i--) { b[p + (7 - i)] = (byte)(v >>> (i*8)); }
        return p + 8;
    }
    private static long getLong(byte[] b, int p) {
        long v = 0;
        for (int i = 0; i < 8; i++) v = (v << 8) | (b[p+i] & 0xffL);
        return v;
    }
}
