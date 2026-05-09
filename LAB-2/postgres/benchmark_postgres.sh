#!/bin/bash

echo "Postgres DB Size:"
psql music_titles_pg -c "SELECT pg_size_pretty(pg_database_size('music_titles_pg'));"

echo "Query Timing:"
time psql music_titles_pg -c "SELECT * FROM titles;"
