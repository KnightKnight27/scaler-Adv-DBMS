package com.example.minidb.transaction;

import java.util.HashSet;
import java.util.Set;

public class Lock {

    private LockType type;

    private final Set<Long> owners =
            new HashSet<>();

    public LockType getType() {
        return type;
    }

    public void setType(
            LockType type) {

        this.type = type;
    }

    public Set<Long> getOwners() {
        return owners;
    }
}