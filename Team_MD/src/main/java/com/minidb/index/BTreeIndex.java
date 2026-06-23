package com.minidb.index;

import com.minidb.storage.Page;
import com.minidb.storage.PageManager;
import com.minidb.storage.RowId;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

public class BTreeIndex {
    private PageManager pageManager;
    private IndexMetadata metadata;
    private String indexFileName;

    public BTreeIndex(String indexFileName) {
        this.indexFileName = indexFileName;
        try {
            this.pageManager = new PageManager(indexFileName);
            loadOrInitializeMetadata();
        } catch (IOException e) {
            throw new RuntimeException("Failed to initialize BTreeIndex", e);
        }
    }

    private void loadOrInitializeMetadata() throws IOException {
        if (pageManager.getNumPages() == 0) {
            metadata = new IndexMetadata();
            pageManager.allocatePage(); // Allocates page 0 (metadata)
            int rootNodeId = allocateNewNode(true).getNodeId(); // Allocates page 1
            metadata.setRootNodeId(rootNodeId);
            saveMetadata();
        } else {
            Page metaPage = pageManager.readPage(0);
            byte[] data = new byte[4096 - Page.HEADER_SIZE];
            System.arraycopy(metaPage.getRawBytes(), Page.HEADER_SIZE, data, 0, data.length);
            metadata = IndexMetadata.deserialize(data);
        }
    }

    private void saveMetadata() throws IOException {
        Page metaPage = pageManager.readPage(0);
        byte[] serializedMeta = metadata.serialize();
        System.arraycopy(serializedMeta, 0, metaPage.getRawBytes(), Page.HEADER_SIZE, serializedMeta.length);
        pageManager.writePage(metaPage);
    }

    private BTreeNode allocateNewNode(boolean isLeaf) throws IOException {
        Page page = pageManager.allocatePage();
        BTreeNode node = new BTreeNode(page.getPageId(), isLeaf);
        saveNode(node);
        metadata.incrementNumNodes();
        saveMetadata();
        return node;
    }

    private BTreeNode loadNode(int nodeId) {
        try {
            Page page = pageManager.readPage(nodeId);
            byte[] data = new byte[4096 - Page.HEADER_SIZE];
            System.arraycopy(page.getRawBytes(), Page.HEADER_SIZE, data, 0, data.length);
            return BTreeNode.deserialize(data);
        } catch (IOException e) {
            throw new RuntimeException("Failed to load node " + nodeId, e);
        }
    }

    private void saveNode(BTreeNode node) {
        try {
            Page page = pageManager.readPage(node.getNodeId());
            byte[] serializedNode = node.serialize();
            System.arraycopy(serializedNode, 0, page.getRawBytes(), Page.HEADER_SIZE, serializedNode.length);
            pageManager.writePage(page);
        } catch (IOException e) {
            throw new RuntimeException("Failed to save node " + node.getNodeId(), e);
        }
    }

    public List<RowId> search(int key) {
        if (metadata.getRootNodeId() == -1) return new ArrayList<>();
        return findLeafNode(key).search(key);
    }

    private BTreeNode findLeafNode(int key) {
        BTreeNode current = loadNode(metadata.getRootNodeId());
        while (!current.isLeaf()) {
            int idx = current.findKeyIndex(key);
            if (idx < current.getNumKeys() && current.getKeys().get(idx) == key) idx++;
            current = loadNode(current.getChildPointers().get(idx));
        }
        return current;
    }

    public void insert(int key, RowId rowId) {
        try {
            BTreeNode root = loadNode(metadata.getRootNodeId());
            if (root.isFull()) {
                BTreeNode newRoot = allocateNewNode(false);
                newRoot.getChildPointers().add(root.getNodeId());
                splitChild(newRoot, 0, root);
                metadata.setRootNodeId(newRoot.getNodeId());
                saveMetadata();
                insertNonFull(newRoot, key, rowId);
            } else {
                insertNonFull(root, key, rowId);
            }
        } catch (IOException e) {
            throw new RuntimeException("Failed to insert", e);
        }
    }

    private void insertNonFull(BTreeNode node, int key, RowId rowId) throws IOException {
        if (node.isLeaf()) {
            node.insertLeaf(key, rowId);
            saveNode(node);
        } else {
            int idx = node.findKeyIndex(key);
            if (idx < node.getNumKeys() && node.getKeys().get(idx) == key) idx++;
            
            BTreeNode child = loadNode(node.getChildPointers().get(idx));
            if (child.isFull()) {
                splitChild(node, idx, child);
                if (key >= node.getKeys().get(idx)) idx++;
                child = loadNode(node.getChildPointers().get(idx));
            }
            insertNonFull(child, key, rowId);
        }
    }

    private void splitChild(BTreeNode parent, int childIndex, BTreeNode child) throws IOException {
        BTreeNode rightNode = allocateNewNode(child.isLeaf());
        int midKey = child.getKeys().get(child.getNumKeys() / 2); // For internal nodes, this key moves up
        
        BTreeNode newRight = child.split(rightNode.getNodeId()); // splits child in place
        
        // Copy the split contents to rightNode
        rightNode = newRight;
        saveNode(child);
        saveNode(rightNode);
        
        // Internal nodes promote the midKey, leaf nodes promote midKey (copy)
        int promoteKey = child.isLeaf() ? rightNode.getKeys().get(0) : midKey;
        
        parent.insertInternal(promoteKey, rightNode.getNodeId());
        saveNode(parent);
    }

    public void delete(int key) {
        try {
            if (metadata.getRootNodeId() == -1) return;
            BTreeNode root = loadNode(metadata.getRootNodeId());
            deleteRecursive(null, -1, root, key);
            
            // If root became empty
            if (root.getNumKeys() == 0) {
                if (!root.isLeaf()) {
                    // Root is internal and empty, its only child becomes the new root
                    metadata.setRootNodeId(root.getChildPointers().get(0));
                    saveMetadata();
                } else {
                    // Root is leaf and empty, tree is empty
                    metadata.setRootNodeId(-1);
                    saveMetadata();
                }
            }
        } catch (IOException e) {
            throw new RuntimeException("Failed to delete", e);
        }
    }

    private void deleteRecursive(BTreeNode parent, int childIndex, BTreeNode current, int key) {
        if (current.isLeaf()) {
            boolean modified = false;
            // Remove all matching keys from the leaf node
            for (int i = current.getNumKeys() - 1; i >= 0; i--) {
                if (current.getKeys().get(i) == key) {
                    current.getKeys().remove(i);
                    current.getLeafValues().remove(i);
                    modified = true;
                }
            }
            if (modified) {
                setNumKeys(current, current.getKeys().size());
                saveNode(current);
            }
        } else {
            int idx = current.findKeyIndex(key);
            if (idx < current.getNumKeys() && current.getKeys().get(idx) == key) {
                idx++;
            }
            
            BTreeNode child = loadNode(current.getChildPointers().get(idx));
            deleteRecursive(current, idx, child, key);
            
            if (child.getNumKeys() < BTreeNode.MAX_KEYS / 2) {
                handleUnderflow(current, idx, child);
            }
        }
    }

    private void handleUnderflow(BTreeNode parent, int childIndex, BTreeNode child) {
        int MIN_KEYS = BTreeNode.MAX_KEYS / 2;
        if (child.getNumKeys() >= MIN_KEYS) return;

        // Try borrow from left sibling
        if (childIndex > 0) {
            BTreeNode leftSibling = loadNode(parent.getChildPointers().get(childIndex - 1));
            if (leftSibling.getNumKeys() > MIN_KEYS) {
                borrowFromLeft(parent, childIndex, leftSibling, child);
                return;
            }
        }

        // Try borrow from right sibling
        if (childIndex < parent.getNumKeys()) {
            BTreeNode rightSibling = loadNode(parent.getChildPointers().get(childIndex + 1));
            if (rightSibling.getNumKeys() > MIN_KEYS) {
                borrowFromRight(parent, childIndex, child, rightSibling);
                return;
            }
        }

        // If borrow fails, merge.
        if (childIndex > 0) {
            BTreeNode leftSibling = loadNode(parent.getChildPointers().get(childIndex - 1));
            mergeNodes(parent, childIndex - 1, leftSibling, child);
        } else if (childIndex < parent.getNumKeys()) {
            BTreeNode rightSibling = loadNode(parent.getChildPointers().get(childIndex + 1));
            mergeNodes(parent, childIndex, child, rightSibling);
        }
    }

    private void borrowFromLeft(BTreeNode parent, int childIndex, BTreeNode left, BTreeNode child) {
        if (child.isLeaf()) {
            int lastIdx = left.getNumKeys() - 1;
            int k = left.getKeys().remove(lastIdx);
            RowId v = left.getLeafValues().remove(lastIdx);
            setNumKeys(left, left.getNumKeys() - 1);
            
            child.getKeys().add(0, k);
            child.getLeafValues().add(0, v);
            setNumKeys(child, child.getNumKeys() + 1);
            
            parent.getKeys().set(childIndex - 1, child.getKeys().get(0));
            
            saveNode(left); saveNode(child); saveNode(parent);
        } else {
            int lastIdx = left.getNumKeys() - 1;
            int k = left.getKeys().remove(lastIdx);
            int ptr = left.getChildPointers().remove(lastIdx + 1);
            setNumKeys(left, left.getNumKeys() - 1);
            
            int parentKey = parent.getKeys().get(childIndex - 1);
            parent.getKeys().set(childIndex - 1, k);
            
            child.getKeys().add(0, parentKey);
            child.getChildPointers().add(0, ptr);
            setNumKeys(child, child.getNumKeys() + 1);
            
            saveNode(left); saveNode(child); saveNode(parent);
        }
    }

    private void borrowFromRight(BTreeNode parent, int childIndex, BTreeNode child, BTreeNode right) {
        if (child.isLeaf()) {
            int k = right.getKeys().remove(0);
            RowId v = right.getLeafValues().remove(0);
            setNumKeys(right, right.getNumKeys() - 1);
            
            child.getKeys().add(k);
            child.getLeafValues().add(v);
            setNumKeys(child, child.getNumKeys() + 1);
            
            parent.getKeys().set(childIndex, right.getKeys().get(0));
            
            saveNode(right); saveNode(child); saveNode(parent);
        } else {
            int k = right.getKeys().remove(0);
            int ptr = right.getChildPointers().remove(0);
            setNumKeys(right, right.getNumKeys() - 1);
            
            int parentKey = parent.getKeys().get(childIndex);
            parent.getKeys().set(childIndex, k);
            
            child.getKeys().add(parentKey);
            child.getChildPointers().add(ptr);
            setNumKeys(child, child.getNumKeys() + 1);
            
            saveNode(right); saveNode(child); saveNode(parent);
        }
    }

    private void mergeNodes(BTreeNode parent, int leftIndex, BTreeNode left, BTreeNode right) {
        if (left.isLeaf()) {
            left.getKeys().addAll(right.getKeys());
            left.getLeafValues().addAll(right.getLeafValues());
            setNumKeys(left, left.getNumKeys() + right.getNumKeys());
            
            left.setNextLeafId(right.getNextLeafId());
            
            parent.getKeys().remove(leftIndex);
            parent.getChildPointers().remove(leftIndex + 1);
            setNumKeys(parent, parent.getNumKeys() - 1);
            
            saveNode(left); saveNode(parent);
        } else {
            int parentKey = parent.getKeys().remove(leftIndex);
            parent.getChildPointers().remove(leftIndex + 1);
            setNumKeys(parent, parent.getNumKeys() - 1);
            
            left.getKeys().add(parentKey);
            left.getKeys().addAll(right.getKeys());
            left.getChildPointers().addAll(right.getChildPointers());
            setNumKeys(left, left.getNumKeys() + 1 + right.getNumKeys());
            
            saveNode(left); saveNode(parent);
        }
    }

    private void setNumKeys(BTreeNode node, int numKeys) {
        try {
            java.lang.reflect.Field field = BTreeNode.class.getDeclaredField("numKeys");
            field.setAccessible(true);
            field.set(node, numKeys);
        } catch (Exception e) {
            throw new RuntimeException("Failed to update numKeys via reflection", e);
        }
    }

    public void close() throws IOException {
        pageManager.close();
    }
}
