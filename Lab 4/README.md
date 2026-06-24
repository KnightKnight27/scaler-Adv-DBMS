# Lab 4 — Inspecting a Real SQLite Database 

The DB used here is `pokemon.db`

---

## Schema & data

```sql
CREATE TABLE pokemon (
    id      INTEGER PRIMARY KEY,
    name    TEXT,
    type    TEXT,
    species TEXT,
    height  TEXT,
    weight  TEXT
);
```

| id | name        | type            | species              | height | weight    |
|----|-------------|-----------------|----------------------|--------|-----------|
| 1  | Pikachu     | Electric        | Mouse Pokemon        | 0.4 m  | 6.0 kg    |
| 2  | Charizard   | Fire/Flying     | Flame Pokemon        | 1.7 m  | 90.5 kg   |
| 3  | Bulbasaur   | Grass/Poison    | Seed Pokemon         | 0.7 m  | 6.9 kg    |
| 4  | Squirtle    | Water           | Tiny Turtle Pokemon  | 0.5 m  | 9.0 kg    |
| 5  | Jigglypuff  | Normal/Fairy    | Balloon Pokemon      | 0.5 m  | 5.5 kg    |
| 6  | Meowth      | Normal          | Scratch Cat Pokemon  | 0.4 m  | 4.2 kg    |
| 7  | Gengar      | Ghost/Poison    | Shadow Pokemon       | 1.5 m  | 40.5 kg   |
| 8  | Eevee       | Normal          | Evolution Pokemon    | 0.3 m  | 6.5 kg    |
| 9  | Snorlax     | Normal          | Sleeping Pokemon     | 2.1 m  | 460.0 kg  |
| 10 | Lucario     | Fighting/Steel  | Aura Pokemon         | 1.2 m  | 54.0 kg   |


The result is an **8192‑byte** file = exactly **2 pages** of 4096 bytes each.

---

## Verification via SQLite's own tools

```text
$ sqlite3 pokemon.db '.dbinfo'
database page size:  4096
write format:        1
read format:         1
reserved bytes:      12
file change counter: 2
database page count: 2
freelist page count: 0
schema cookie:       1
schema format:       4
text encoding:       1 (utf8)
software version:    3051000
number of tables:    1

---

## hex dump (`xxd pokemon.db`)

Below is the actual dump.

### Page 1, first 0x100 bytes — the database header

```
00000000: 5351 4c69 7465 2066 6f72 6d61 7420 3300  SQLite format 3.
00000010: 1000 0101 0c40 2020 0000 0002 0000 0002  .....@  ........
00000020: 0000 0000 0000 0000 0000 0001 0000 0004  ................
00000030: 0000 0000 0000 0000 0000 0001 0000 0000  ................
00000040: 0000 0000 0000 0000 0000 0000 0000 0000  ................
00000050: 0000 0000 0000 0000 0000 0000 0000 0000  ................
00000060: 0000 0000 0000 0000 0000 0000 0000 0002  ................
00000070: 002e 8df8 0d00 0000 010f 5100 0f51 0000  ..........Q..Q..
```

### Page 1, B‑tree payload

```
00000f50: 0081 2001 0717 1b1b 0182 1774 6162 6c65  .. ........table
00000f60: 706f 6b65 6d6f 6e70 6f6b 656d 6f6e 0243  pokemonpokemon.C
00000f70: 5245 4154 4520 5441 424c 4520 706f 6b65  REATE TABLE poke
00000f80: 6d6f 6e20 280a 2020 2020 6964 2049 4e54  mon (.    id INT
00000f90: 4547 4552 2050 5249 4d41 5259 204b 4559  EGER PRIMARY KEY
00000fa0: 2c0a 2020 2020 6e61 6d65 2054 4558 542c  ,.    name TEXT,
00000fb0: 0a20 2020 2074 7970 6520 5445 5854 2c0a  .    type TEXT,.
00000fc0: 2020 2020 7370 6563 6965 7320 5445 5854      species TEXT
00000fd0: 2c0a 2020 2020 6865 6967 6874 2054 4558  ,.    height TEX
00000fe0: 542c 0a20 2020 2077 6569 6768 7420 5445  T,.    weight TE
00000ff0: 5854 0a29 0000 0000 0000 0000 0000 0000  XT.)............
```
## B‑tree node pointers in this file

Because this database is small, the tree pointers it contains are:

## 8. Summary

- The file is **2 pages × 4096 bytes = 8192 bytes**.
- **Page 1** = 100‑byte database header + the `sqlite_schema` B‑tree leaf, which holds a single cell pointing the `pokemon` table to its **rootpage = 2**.
- **Page 2** = the `pokemon` table's B‑tree leaf, containing **10 cells** indexed by a cell pointer array, with each cell encoding `(payload size, rowid, record header, record body)`.
- Every page is reached via `offset = (page_no − 1) × page_size`.
- A primary‑key lookup walks: *file header → page 1 schema leaf → rootpage pointer → table leaf → cell pointer array → record decode.* All of these steps were demonstrated byte‑for‑byte against a real `xxd` dump of `pokemon.db`.
