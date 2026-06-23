package com.minidb.index;

import com.minidb.storage.RowId;

import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;

public class BTreeNode {
    public static final int MAX_KEYS = 10;
    
    private int nodeId;
    private boolean isLeaf;
    private int numKeys;
    private List<Integer> keys;
    private List<RowId> leafValues;
    private int nextLeafId;
    private List<Integer> childPointers;

    public BTreeNode(int nodeId, boolean isLeaf) {
        this.nodeId = nodeId;
        this.isLeaf = isLeaf;
        this.numKeys = 0;
        this.keys = new ArrayList<>();
        if (isLeaf) {
            this.leafValues = new ArrayList<>();
            this.nextLeafId = -1;
        } else {
            this.childPointers = new ArrayList<>();
        }
    }

    public int getNodeId() { return nodeId; }
    public boolean isLeaf() { return isLeaf; }
    public int getNumKeys() { return numKeys; }
    public List<Integer> getKeys() { return keys; }
    public List<RowId> getLeafValues() { return leafValues; }
    public int getNextLeafId() { return nextLeafId; }
    public void setNextLeafId(int nextLeafId) { this.nextLeafId = nextLeafId; }
    public List<Integer> getChildPointers() { return childPointers; }
    public boolean isFull() { return numKeys >= MAX_KEYS; }

    public int findKeyIndex(int key) {
        int left = 0, right = numKeys - 1;
        while (left <= right) {
            int mid = left + (right - left) / 2;
            if (keys.get(mid) == key) return mid;
            else if (keys.get(mid) < key) left = mid + 1;
            else right = mid - 1;
        }
        return left;
    }

    public List<RowId> search(int key) {
        if (!isLeaf) throw new IllegalStateException("search() on non-leaf");
        List<RowId> results = new ArrayList<>();
        for (int i = 0; i < numKeys; i++) {
            if (keys.get(i) == key) results.add(leafValues.get(i));
        }
        return results;
    }

    public void insertLeaf(int key, RowId value) {
        int idx = findKeyIndex(key);
        // Find upper bound for duplicates or exact match
        while (idx < numKeys && keys.get(idx) <= key) idx++;
        keys.add(idx, key);
        leafValues.add(idx, value);
        numKeys++;
    }

    public void insertInternal(int key, int rightChildNodeId) {
        int idx = findKeyIndex(key);
        while (idx < numKeys && keys.get(idx) <= key) idx++;
        keys.add(idx, key);
        childPointers.add(idx + 1, rightChildNodeId);
        numKeys++;
    }

    public BTreeNode split(int newNodeId) {
        BTreeNode rightNode = new BTreeNode(newNodeId, this.isLeaf);
        int mid = numKeys / 2;
        
        rightNode.numKeys = numKeys - mid;
        for (int i = mid; i < numKeys; i++) rightNode.keys.add(keys.get(i));
        
        if (isLeaf) {
            for (int i = mid; i < numKeys; i++) rightNode.leafValues.add(leafValues.get(i));
            rightNode.nextLeafId = this.nextLeafId;
            this.nextLeafId = rightNode.nodeId;
        } else {
            // Internal nodes move up the mid key, and right node gets mid+1 to end keys
            rightNode.numKeys = numKeys - mid - 1;
            rightNode.keys.clear();
            for (int i = mid + 1; i < numKeys; i++) rightNode.keys.add(keys.get(i));
            for (int i = mid + 1; i <= numKeys; i++) rightNode.childPointers.add(childPointers.get(i));
        }
        
        this.numKeys = mid;
        this.keys.subList(mid, keys.size()).clear();
        if (isLeaf) {
            this.leafValues.subList(mid, leafValues.size()).clear();
        } else {
            this.childPointers.subList(mid + 1, childPointers.size()).clear();
        }
        
        return rightNode;
    }

    public byte[] serialize() {
        ByteBuffer buffer = ByteBuffer.allocate(4096 - 12);
        buffer.put((byte) (isLeaf ? 1 : 0));
        buffer.putInt(nodeId);
        buffer.putInt(numKeys);
        
        for (int i = 0; i < MAX_KEYS; i++) buffer.putInt(i < numKeys ? keys.get(i) : 0);

        if (isLeaf) {
            for (int i = 0; i < MAX_KEYS; i++) {
                if (i < numKeys) {
                    buffer.putInt(leafValues.get(i).pageId());
                    buffer.putInt(leafValues.get(i).slotNumber());
                } else {
                    buffer.putInt(0);
                    buffer.putInt(0);
                }
            }
            buffer.putInt(nextLeafId);
        } else {
            for (int i = 0; i <= MAX_KEYS; i++) {
                buffer.putInt(i < childPointers.size() ? childPointers.get(i) : 0);
            }
        }
        return buffer.array();
    }

    public static BTreeNode deserialize(byte[] data) {
        ByteBuffer buffer = ByteBuffer.wrap(data);
        boolean isLeaf = buffer.get() == 1;
        int nodeId = buffer.getInt();
        int numKeys = buffer.getInt();

        BTreeNode node = new BTreeNode(nodeId, isLeaf);
        node.numKeys = numKeys;

        for (int i = 0; i < MAX_KEYS; i++) {
            int key = buffer.getInt();
            if (i < numKeys) node.keys.add(key);
        }

        if (isLeaf) {
            for (int i = 0; i < MAX_KEYS; i++) {
                int pageId = buffer.getInt();
                int slotNum = buffer.getInt();
                if (i < numKeys) node.leafValues.add(new RowId(pageId, slotNum));
            }
            node.nextLeafId = buffer.getInt();
        } else {
            for (int i = 0; i <= MAX_KEYS; i++) {
                int childPtr = buffer.getInt();
                if (i <= numKeys) node.childPointers.add(childPtr);
            }
        }
        return node;
    }
}
