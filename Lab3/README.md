# Lab 3 - Clock Sweep Algorithm

## Name
Aparna Singha

## Roll Number
24bcs10353

## Objective

To implement the Clock Sweep page replacement algorithm in C++ and simulate how a database buffer manager chooses a page for replacement when the buffer is full.

## Problem Statement

In database systems, the buffer pool has limited memory. When a new page has to be loaded and all frames are already occupied, the system must decide which page should be removed. The Clock Sweep algorithm is an efficient approximation of the LRU page replacement policy.

## About Clock Sweep Algorithm

Clock Sweep maintains a circular list of frames and a clock hand pointer. Each frame has a reference bit.

When a page is accessed:
- If the page is already present, it is a page hit.
- Its reference bit is set to 1.

When a new page must be inserted:
- If an empty frame is available, the page is inserted directly.
- If the buffer is full, the clock hand starts scanning frames.
- If a frame has reference bit 1, the algorithm clears it and gives the page a second chance.
- If a frame has reference bit 0, that page is replaced.

## Files Included

```text
clock_sweep.cpp
README.md