-- Seed for Lab 4 — Jaivardhan D Rao (24BCS10117)
--
-- We force the `movies` B-tree to split into an interior page + multiple
-- leaves by giving each row a ~1100-byte synopsis. At 4096-byte pages a
-- single leaf can hold roughly 3 such rows, so 25 rows is enough to grow
-- the tree by one level (one interior page on top of several leaves).
--
-- Two tables are created on purpose:
--   * movies   — the table whose hex dump we will analyse
--   * studios  — a small table so the dump shows more than one schema entry

DROP TABLE IF EXISTS movies;
DROP TABLE IF EXISTS studios;

CREATE TABLE movies (
    id       INTEGER PRIMARY KEY,
    title    TEXT NOT NULL,
    synopsis TEXT NOT NULL
);

CREATE TABLE studios (
    id      INTEGER PRIMARY KEY,
    name    TEXT NOT NULL,
    country TEXT NOT NULL
);

-- ---------- movies (25 rows, ~1100-byte synopsis each) ----------
-- Each title is a real-ish movie name; the synopsis is deterministic padding
-- so the dump is reproducible.
INSERT INTO movies(id, title, synopsis) VALUES
 ( 1, 'Inception',                   'M01 ' || hex(zeroblob(548))),
 ( 2, 'The Matrix',                  'M02 ' || hex(zeroblob(548))),
 ( 3, 'Interstellar',                'M03 ' || hex(zeroblob(548))),
 ( 4, 'Pulp Fiction',                'M04 ' || hex(zeroblob(548))),
 ( 5, 'Parasite',                    'M05 ' || hex(zeroblob(548))),
 ( 6, 'The Godfather',               'M06 ' || hex(zeroblob(548))),
 ( 7, 'Schindlers List',             'M07 ' || hex(zeroblob(548))),
 ( 8, 'Forrest Gump',                'M08 ' || hex(zeroblob(548))),
 ( 9, 'The Dark Knight',             'M09 ' || hex(zeroblob(548))),
 (10, 'Fight Club',                  'M10 ' || hex(zeroblob(548))),
 (11, 'Goodfellas',                  'M11 ' || hex(zeroblob(548))),
 (12, 'The Shawshank Redemption',    'M12 ' || hex(zeroblob(548))),
 (13, 'Whiplash',                    'M13 ' || hex(zeroblob(548))),
 (14, 'The Departed',                'M14 ' || hex(zeroblob(548))),
 (15, 'Gladiator',                   'M15 ' || hex(zeroblob(548))),
 (16, 'The Prestige',                'M16 ' || hex(zeroblob(548))),
 (17, 'Memento',                     'M17 ' || hex(zeroblob(548))),
 (18, 'Spirited Away',               'M18 ' || hex(zeroblob(548))),
 (19, 'Joker',                       'M19 ' || hex(zeroblob(548))),
 (20, 'Oppenheimer',                 'M20 ' || hex(zeroblob(548))),
 (21, 'Dune Part Two',               'M21 ' || hex(zeroblob(548))),
 (22, 'La La Land',                  'M22 ' || hex(zeroblob(548))),
 (23, 'No Country For Old Men',      'M23 ' || hex(zeroblob(548))),
 (24, 'There Will Be Blood',         'M24 ' || hex(zeroblob(548))),
 (25, 'The Social Network',          'M25 ' || hex(zeroblob(548)));

-- ---------- studios (5 rows, small) ----------
INSERT INTO studios(id, name, country) VALUES
 (1, 'Warner Bros',      'USA'),
 (2, 'Paramount',        'USA'),
 (3, 'Studio Ghibli',    'Japan'),
 (4, 'CJ Entertainment', 'South Korea'),
 (5, 'A24',              'USA');
