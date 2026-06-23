package com.minidb.storage;

/**
 * A simple immutable identifier for a row, consisting of a page ID and a slot number.
 */
public record RowId(int pageId, int slotNumber) {
    // Records automatically handle equals, hashCode, and toString.
}
