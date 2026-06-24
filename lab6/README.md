# Lab 6: B-Tree Index

This lab implements a small B-tree index in C++.

## Features

- `DB<Key, Row>` template class
- Stores `key -> row` entries
- Insert operation
- Search operation
- Sorted display of index entries
- Node splitting when a B-tree node becomes full

## Build

```powershell
cmake -S lab6 -B lab6/build
cmake --build lab6/build
```

## Run

```powershell
.\lab6\build\Debug\BTreeIndex.exe
```

For single-config generators, the executable may be:

```powershell
.\lab6\build\BTreeIndex.exe
```
