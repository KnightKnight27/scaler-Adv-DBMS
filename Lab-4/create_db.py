import sqlite3

conn = sqlite3.connect('my_database.db')
cursor = conn.cursor()
cursor.execute('CREATE TABLE IF NOT EXISTS sample(id INTEGER PRIMARY KEY, name TEXT);')
cursor.execute('DELETE FROM sample;') # clear just in case
cursor.execute("INSERT INTO sample(name) VALUES ('Alice'), ('Bob'), ('Charlie');")
conn.commit()
conn.close()
