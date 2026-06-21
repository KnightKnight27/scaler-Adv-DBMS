#!/bin/bash

echo "SQLite Page Size:"
sqlite3 music_titles.db "PRAGMA page_size;"

echo "SQLite Page Count:"
sqlite3 music_titles.db "PRAGMA page_count;"

echo "Without mmap:"
sqlite3 music_titles.db "PRAGMA mmap_size=0;"
time sqlite3 music_titles.db "SELECT * FROM titles;"

echo "With mmap:"
sqlite3 music_titles.db "PRAGMA mmap_size=268435456;"
time sqlite3 music_titles.db "SELECT * FROM titles;"
