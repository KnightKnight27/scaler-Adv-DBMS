package com.minidb.index;

import java.nio.ByteBuffer;

public class IndexMetadata {
    public static final int MAGIC_NUMBER = 0xDEADBEEF;
    private int magic = MAGIC_NUMBER;
    private int version = 1;
    private int rootNodeId = -1; // -1 means no root yet
    private int numNodes = 0;

    public IndexMetadata() {
    }

    public IndexMetadata(int rootNodeId, int numNodes) {
        this.rootNodeId = rootNodeId;
        this.numNodes = numNodes;
    }

    public int getRootNodeId() {
        return rootNodeId;
    }

    public void setRootNodeId(int rootNodeId) {
        this.rootNodeId = rootNodeId;
    }

    public int getNumNodes() {
        return numNodes;
    }

    public void setNumNodes(int numNodes) {
        this.numNodes = numNodes;
    }

    public void incrementNumNodes() {
        this.numNodes++;
    }

    public byte[] serialize() {
        ByteBuffer buffer = ByteBuffer.allocate(4084);
        buffer.putInt(magic);
        buffer.putInt(version);
        buffer.putInt(rootNodeId);
        buffer.putInt(numNodes);
        return buffer.array();
    }

    public static IndexMetadata deserialize(byte[] data) {
        ByteBuffer buffer = ByteBuffer.wrap(data);
        int magic = buffer.getInt();
        if (magic != MAGIC_NUMBER) {
            throw new IllegalArgumentException("Invalid index metadata magic number: " + Integer.toHexString(magic));
        }
        int version = buffer.getInt();
        int rootNodeId = buffer.getInt();
        int numNodes = buffer.getInt();
        
        IndexMetadata meta = new IndexMetadata();
        meta.magic = magic;
        meta.version = version;
        meta.rootNodeId = rootNodeId;
        meta.numNodes = numNodes;
        return meta;
    }
}
