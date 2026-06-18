# scaler-Adv-DBMS
# SQLite3 Internal Storage Analysis using `xxd`

# Objective

The objective of this assignment is to:

* Examine the internal binary structure of a SQLite3 database file using `xxd`
* Understand SQLite page layout
* Understand SQLite B-Tree node structure
* Identify:

  * database header
  * page headers
  * cell pointer arrays
  * B-tree nodes
  * record payloads
  * addresses and offsets
* Trace actual data stored inside a real database

The database used in this assignment is a custom Pokémon-themed database (`pokedex.db`) containing:

* `pokemon` table
* `trainers` table

---

# Tools Used

| Tool                 | Purpose                     |
| -------------------- | --------------------------- |
| `sqlite3`            | Create and inspect database |
| `xxd`                | Generate hexadecimal dump   |
| `hexdump`            | Optional binary inspection  |
| Linux terminal / WSL | Execution environment       |

---

# Database Creation

## Database Schema

```sql
CREATE TABLE pokemon (
    id INTEGER PRIMARY KEY,
    name TEXT,
    type1 TEXT,
    type2 TEXT,
    hp INTEGER,
    attack INTEGER,
    defense INTEGER
);

CREATE TABLE trainers (
    id INTEGER PRIMARY KEY,
    trainer_name TEXT,
    hometown TEXT
);
```

---

# Sample Data

## Pokémon Entries

| id | name       | type1    | type2  | hp | attack | defence |
| -- | ---------- | -------- | ------ | -- | ------ | ------- | 
| 1  | Bulbasaur  | Grass    | Poison | 45 | 49     | 49      |
| 2  | Ivysaur    | Grass    | Poison | 60 | 62     | 63      |
| 3  | Venusaur   | Grass    | Poison | 80 | 82     | 83      |
| 4  | Charmander | Fire     | NULL   | 39 | 52     | 43      |
| 5  | Charmeleon | Fire     | NULL   | 58 | 64     | 58      |
| 6  | Charizard  | Fire     | Flying | 78 | 84     | 78      |
| 7  | Squirtle   | Water    | NULL   |44  | 48     | 65      |
| 8  | Wartortle  | Water    | NULL   |59  | 63     | 80      |
| 9  | Blastoise  | Water    | NULL   |79  | 83     | 100     | 
| 10 | Pikachu    | Electric | NULL   |35  | 55     | 40      |

---

## Trainer Entries

| id | trainer_name | hometown      |
| -- | ------------ | ------------- |
| 1  | Ash          | Pallet Town   |
| 2  | Misty        | Cerulean City |
| 3  | Brock        | Pewter City   |

---

# Generating Hex Dump

Command used:

```bash
xxd pokedex.db > dump.txt
```

---

# SQLite File Header Analysis

The first 100 bytes of every SQLite database contain the database header.

## Actual Hex Dump

```text
00000000: 5351 4c69 7465 2066 6f72 6d61 7420 3300
00000010: 1000 0101 0040 2020 0000 0004 0000 0003
00000020: 0000 0000 0000 0000 0000 0002 0000 0004
```

---

# Header Breakdown

| Offset      | Bytes               | Meaning                           |
| ----------- | ------------------- | --------------------------------- |
| `0x00–0x0F` | `SQLite format 3\0` | SQLite signature                  |
| `0x10–0x11` | `1000`              | Page size = `0x1000 = 4096 bytes` |
| `0x12`      | `01`                | File format write version         |
| `0x13`      | `01`                | File format read version          |
| `0x14`      | `00`                | Reserved space                    |
| `0x15`      | `40`                | Max embedded payload fraction     |
| `0x16`      | `20`                | Min embedded payload fraction     |
| `0x17`      | `20`                | Leaf payload fraction             |
| `0x18–0x1B` | `00000004`          | File change counter               |
| `0x1C–0x1F` | `00000003`          | Database size in pages            |

---

# SQLite Page Structure

SQLite databases are divided into fixed-size pages.

## In this database:

```text
Page Size = 4096 bytes
```

Therefore:

| Page Number | Address Range     |
| ----------- | ----------------- |
| Page 1      | `0x0000 – 0x0FFF` |
| Page 2      | `0x1000 – 0x1FFF` |
| Page 3      | `0x2000 – 0x2FFF` |

---

# B-Tree Overview

SQLite stores tables using B-Trees.

## Types of Pages

| Page Type      | Hex Value | Meaning             |
| -------------- | --------- | ------------------- |
| Interior Index | `02`      | Internal index node |
| Interior Table | `05`      | Internal table node |
| Leaf Index     | `0A`      | Leaf index node     |
| Leaf Table     | `0D`      | Leaf table node     |

---

# Page 1 Analysis — SQLite Master Table

## Beginning of Page 1

```text
00000060: 002e 7689 0d00 0000 020e ca00 0f47 0eca
```

---

# Page Header Structure

Starting at offset `0x64`:

```text
0d 00 00 00 02 0e ca 00 0f 47 0e ca
```

---

# Interpretation

| Offset      | Value  | Meaning                    |
| ----------- | ------ | -------------------------- |
| `0x64`      | `0D`   | Leaf table B-tree page     |
| `0x65–0x66` | `0000` | First freeblock            |
| `0x67–0x68` | `0002` | Number of cells = 2        |
| `0x69–0x6A` | `0ECA` | Start of cell content area |
| `0x6B`      | `00`   | Fragmented free bytes      |
| `0x6C–0x6D` | `0F47` | Cell pointer #1            |
| `0x6E–0x6F` | `0ECA` | Cell pointer #2            |

---

# Meaning of Cell Pointers

Cell pointers contain offsets to records inside the page.

## Cell Locations

| Pointer  | Address              |
| -------- | -------------------- |
| `0x0F47` | `0x0F47` inside page |
| `0x0ECA` | `0x0ECA` inside page |

These locations contain the actual records.

---

# SQLite Master Table Records

SQLite internally stores schema definitions inside `sqlite_master`.

At address:

```text
00000ed0:
```

we observe:

```text
7461 626c 6574 7261 696e 657273
```

ASCII conversion:

```text
tabletrainers
```

This corresponds to:

```sql
CREATE TABLE trainers (...)
```

---

# Trainers Table Schema Record

## Hex Dump

```text
00000ed0: 0181 4974 6162 6c65 7472 6169 6e65 7273
00000ee0: 7472 6169 6e65 7273 0343 5245 4154 4520
00000ef0: 5441 424c 4520 7472 6169 6e65 7273 2028
```

---

# ASCII Interpretation

```text
table trainers trainers CREATE TABLE trainers (
```

This record contains:

| Field       | Value                  |
| ----------- | ---------------------- |
| Object Type | table                  |
| Table Name  | trainers               |
| Root Page   | 3                      |
| SQL Schema  | CREATE TABLE statement |

---

# Pokémon Table Schema Record

At:

```text
00000f50:
```

we observe:

```text
tablepokemonpokemon
```

This corresponds to the `pokemon` table schema.

---

# Pokémon Table Root Page

Inside the schema entry:

```text
... pokemon 02 CREATE TABLE ...
```

The value `02` indicates:

```text
pokemon table root page = Page 2
```

---

# Trainers Table Root Page

Similarly:

```text
... trainers 03 CREATE TABLE ...
```

indicates:

```text
trainers table root page = Page 3
```

---

# Page 2 Analysis — Pokémon Table Data

Page 2 starts at:

```text
0x1000
```

---

# Page 2 Header

```text
00001000: 0d00 0000 0a0e de00
```

---

# Interpretation

| Offset          | Value  | Meaning                    |
| --------------- | ------ | -------------------------- |
| `0x1000`        | `0D`   | Leaf table B-tree page     |
| `0x1003–0x1004` | `000A` | Number of cells = 10       |
| `0x1005–0x1006` | `0EDE` | Start of cell content area |

This means the Pokémon table contains 10 records.

---

# Cell Pointer Array

```text
0fdf 0fc0 0fa0 0f85
0f6a 0f4a 0f30 0f15
0efa 0ede
```

---

# Meaning

Each pointer references a row stored later in the page.

| Cell | Offset   |
| ---- | -------- |
| 1    | `0x0FDF` |
| 2    | `0x0FC0` |
| 3    | `0x0FA0` |
| 4    | `0x0F85` |
| 5    | `0x0F6A` |
| 6    | `0x0F4A` |
| 7    | `0x0F30` |
| 8    | `0x0F15` |
| 9    | `0x0EFA` |
| 10   | `0x0EDE` |

---

# Pokémon Record Analysis

## Example Record — Pikachu

Located near:

```text
00001ee0
```

Hex:

```text
5069 6b61 6368 75
```

ASCII:

```text
Pikachu
```

---

# Full Data Region

```text
PikachuElectric
BlastoiseWater
WartortleWater
SquirtleWater
CharizardFireFlying
CharmeleonFire
CharmanderFire
VenusaurGrassPoison
IvysaurGrassPoison
BulbasaurGrassPoison
```

This confirms:

* SQLite stores TEXT directly in payload sections
* Records are packed near the end of the page
* Cell pointers reference these payloads

---

# Pokémon Table B-Tree Structure

```text
Page 2 (Leaf Table B-Tree)
│
├── Cell Pointer Array
│    ├── 0x0FDF → Pikachu
│    ├── 0x0FC0 → Blastoise
│    ├── 0x0FA0 → Wartortle
│    ├── ...
│
└── Cell Content Area
     ├── Serialized rows
     ├── Record headers
     ├── Varints
     └── Payload data
```

---

# Page 3 Analysis — Trainers Table

Page 3 starts at:

```text
0x2000
```

---

# Page 3 Header

```text
00002000: 0d00 0000 030f be00
```

---

# Interpretation

| Offset          | Value                  |
| --------------- | ---------------------- |
| Page Type       | `0D` (leaf table page) |
| Number of Cells | `0003`                 |
| Content Start   | `0FBE`                 |

This page stores 3 trainer rows.

---

# Trainer Records

At:

```text
00002fc0
```

we observe:

```text
BrockPewter City
```

At:

```text
00002fd0`
```

we observe:

```text
MistyCerulean City
```

At:

```text
00002ff0`
```

we observe:

```text
AshPallet Town
```

---

# Trainer Page Structure

```text
Page 3 (Leaf Table B-Tree)
│
├── Cell Pointer Array
│    ├── Trainer Row 1
│    ├── Trainer Row 2
│    └── Trainer Row 3
│
└── Payload Region
     ├── Ash
     ├── Misty
     └── Brock
```

---

# Record Storage Format

SQLite records contain:

1. Payload Length
2. Row ID
3. Record Header
4. Serial Type Array
5. Actual Data

---

# SQLite Varints

SQLite uses variable-length integers called **varints**.

Advantages:

* Smaller storage
* Efficient encoding
* Compact records

Used for:

* Row IDs
* Payload lengths
* Header sizes

---

# B-Tree Navigation Process

## Record Lookup Example

Suppose we query:

```sql
SELECT * FROM pokemon WHERE id = 6;
```

SQLite performs:

1. Open root page (Page 2)
2. Read B-tree page header
3. Traverse cell pointers
4. Compare rowids
5. Locate correct payload
6. Deserialize record

---

# Address Navigation Summary

| Structure     | Address           |
| ------------- | ----------------- |
| SQLite Header | `0x0000`          |
| Page 1 Start  | `0x0000`          |
| Page 2 Start  | `0x1000`          |
| Page 3 Start  | `0x2000`          |
| Pokémon Data  | `0x1EE0 – 0x1FFF` |
| Trainer Data  | `0x2FC0 – 0x2FFF` |

---

# Overall Database Structure

```text
SQLite Database File
│
├── Database Header (100 bytes)
│
├── Page 1
│   ├── sqlite_master B-tree
│   ├── pokemon schema
│   └── trainers schema
│
├── Page 2
│   ├── pokemon table B-tree
│   └── Pokémon rows
│
└── Page 3
    ├── trainers table B-tree
    └── Trainer rows
```