#!/usr/bin/env bash
# Lab 2 — one-time setup for SQLite3 + PostgreSQL on Ubuntu (WSL2).
# Run once with:  bash Lab2/setup.sh
set -e

echo ">>> apt update"
sudo apt update -y

echo ">>> Installing sqlite3 and postgresql"
sudo apt install -y sqlite3 postgresql postgresql-contrib

echo ">>> Starting postgresql service"
sudo service postgresql start

echo ">>> Verifying installs"
sqlite3 --version
psql --version

echo
echo "Setup complete. You can now run: bash Lab2/run_all.sh"
