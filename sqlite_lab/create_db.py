import sqlite3
import os

db_path = 'lab.db'

# If database file already exists, delete it to start fresh
if os.path.exists(db_path):
    os.remove(db_path)

# Connect to SQLite database
conn = sqlite3.connect(db_path)
cursor = conn.cursor()

# Set page size to 512 bytes for compact hex dump and easy visualization
cursor.execute('PRAGMA page_size = 512;')
# Force write to disk immediately and set journaling to off for simple database structure without WAL files
cursor.execute('PRAGMA journal_mode = OFF;')

# Create a table
cursor.execute('''
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    role TEXT NOT NULL
);
''')

# Insert data rows
users_data = [
    (1, 'Alice', 'Admin'),
    (2, 'Bob', 'Contributor'),
    (3, 'Charlie', 'Maintainer')
]

cursor.executemany('INSERT INTO users (id, name, role) VALUES (?, ?, ?);', users_data)

# Commit and close
conn.commit()
conn.close()

print("Database 'lab.db' created successfully with page size 512.")
