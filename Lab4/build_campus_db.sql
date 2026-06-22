-- Lab 4 : SQLite on-disk structure study
-- Author: 24BCS10345 Ansh Mahajan
--
-- This script builds the campus.db file that we later dissect byte-by-byte.
-- Two small rowid tables are enough to surface every structure the lab asks
-- about: the file header, the schema b-tree on page 1, and table leaf pages.

PRAGMA page_size = 4096;     -- fix the page size so the dump is reproducible
PRAGMA encoding  = 'UTF-8';

CREATE TABLE student (
    roll    INTEGER PRIMARY KEY,   -- becomes the rowid -> no separate key column on disk
    name    TEXT    NOT NULL,
    branch  TEXT    NOT NULL,
    cgpa    REAL
);

CREATE TABLE club (
    club_id INTEGER PRIMARY KEY,
    title   TEXT    NOT NULL,
    mentor  TEXT
);

INSERT INTO student (roll, name, branch, cgpa) VALUES
    (10345, 'Ansh Mahajan',   'CSE', 8.9),
    (10212, 'Riya Sharma',    'ECE', 9.1),
    (10477, 'Karan Mehta',    'CSE', 7.8),
    (10560, 'Sneha Iyer',     'MEC', 8.2),
    (10688, 'Vikram Bose',    'CSE', 8.6);

INSERT INTO club (club_id, title, mentor) VALUES
    (1, 'Coding Society',  'Dr. Verma'),
    (2, 'Robotics Guild',  'Dr. Nair'),
    (3, 'Literary Circle', 'Ms. Dutta');
