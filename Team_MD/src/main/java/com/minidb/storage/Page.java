package com.minidb.storage;

import java.nio.ByteBuffer;

/**
 * Represents a fixed-size 4096-byte page using a slotted-page design.
 * 
 * Layout:
 * [Header: pageId(4), slotCount(4), freeSpacePointer(4)]
 * [Slot Directory: (offset:4, length:4) x slotCount]
 * [... Free Space ...]
 * [Row Data: Grows backward from the end of the page]
 */
public class Page {
    public static final int PAGE_SIZE = 4096;
    public static final int HEADER_SIZE = 12; // pageId, slotCount, freeSpacePointer
    public static final int SLOT_SIZE = 8;    // offset, length

    private final byte[] data;
    private int pageId;
    private int slotCount;
    private int freeSpacePointer; // Lowest byte address of used data space (grows backward from 4096)

    public Page(int pageId) {
        this.data = new byte[PAGE_SIZE];
        this.pageId = pageId;
        this.slotCount = 0;
        this.freeSpacePointer = PAGE_SIZE;
        updateHeader();
    }

    private Page(byte[] data) {
        this.data = data;
        ByteBuffer buffer = ByteBuffer.wrap(data);
        this.pageId = buffer.getInt();
        this.slotCount = buffer.getInt();
        this.freeSpacePointer = buffer.getInt();
    }

    public static Page fromBytes(byte[] raw) {
        return new Page(raw);
    }

    public byte[] getRawBytes() {
        return data;
    }

    public int getPageId() {
        return pageId;
    }

    public int getSlotCount() {
        return slotCount;
    }

    /**
     * Inserts a row into the page.
     * Returns the slot number, or -1 if no space.
     */
    public int insertRow(byte[] rowBytes) {
        int spaceRequired = SLOT_SIZE + rowBytes.length;
        if (getFreeSpace() < spaceRequired) {
            return -1;
        }

        // Write row data at the freeSpacePointer
        freeSpacePointer -= rowBytes.length;
        System.arraycopy(rowBytes, 0, data, freeSpacePointer, rowBytes.length);

        // Find an empty slot or create a new one
        int slotNumber = -1;
        for (int i = 0; i < slotCount; i++) {
            if (getSlotLength(i) == -1) { // Deleted slot
                slotNumber = i;
                break;
            }
        }

        if (slotNumber == -1) {
            slotNumber = slotCount++;
        }

        setSlot(slotNumber, freeSpacePointer, rowBytes.length);
        updateHeader();
        return slotNumber;
    }

    public byte[] getRow(int slotNumber) {
        if (slotNumber >= slotCount) return null;
        int offset = getSlotOffset(slotNumber);
        int length = getSlotLength(slotNumber);
        if (length == -1) return null; // Deleted

        byte[] rowBytes = new byte[length];
        System.arraycopy(data, offset, rowBytes, 0, length);
        return rowBytes;
    }

    public void deleteRow(int slotNumber) {
        if (slotNumber < slotCount) {
            setSlot(slotNumber, 0, -1); // Mark as deleted
            updateHeader();
            // Note: We don't reclaim space yet as per requirements.
        }
    }

    public boolean updateRow(int slotNumber, byte[] newBytes) {
        if (slotNumber >= slotCount) return false;
        int offset = getSlotOffset(slotNumber);
        int length = getSlotLength(slotNumber);
        if (length == -1 || length != newBytes.length) {
            return false; // Can only do in-place updates of exact same length
        }
        System.arraycopy(newBytes, 0, data, offset, length);
        return true;
    }

    /**
     * Restores a previously-deleted slot with the given bytes (used by undo phase).
     * Returns true if the slot was found and restored; false if slot doesn't exist.
     */
    public boolean restoreRow(int slotNumber, byte[] rowBytes) {
        if (slotNumber >= slotCount) return false;
        int length = getSlotLength(slotNumber);
        if (length != -1) {
            // Slot already live — overwrite in place if same length
            if (length == rowBytes.length) {
                int offset = getSlotOffset(slotNumber);
                System.arraycopy(rowBytes, 0, data, offset, rowBytes.length);
                return true;
            }
            return false;
        }
        // Slot is deleted (-1): re-use its space if we can write at the current freeSpacePointer
        freeSpacePointer -= rowBytes.length;
        System.arraycopy(rowBytes, 0, data, freeSpacePointer, rowBytes.length);
        setSlot(slotNumber, freeSpacePointer, rowBytes.length);
        updateHeader();
        return true;
    }

    public int getFreeSpace() {
        // Space between end of slot directory and start of data
        return freeSpacePointer - (HEADER_SIZE + (slotCount * SLOT_SIZE));
    }

    private void setSlot(int index, int offset, int length) {
        int pos = HEADER_SIZE + (index * SLOT_SIZE);
        ByteBuffer.wrap(data, pos, SLOT_SIZE).putInt(offset).putInt(length);
    }

    private int getSlotOffset(int index) {
        return ByteBuffer.wrap(data, HEADER_SIZE + (index * SLOT_SIZE), 4).getInt();
    }

    private int getSlotLength(int index) {
        return ByteBuffer.wrap(data, HEADER_SIZE + (index * SLOT_SIZE) + 4, 4).getInt();
    }

    private void updateHeader() {
        ByteBuffer buffer = ByteBuffer.wrap(data, 0, HEADER_SIZE);
        buffer.putInt(pageId);
        buffer.putInt(slotCount);
        buffer.putInt(freeSpacePointer);
    }
}
