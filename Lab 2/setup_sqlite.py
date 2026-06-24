import sqlite3
import random
import string

def create_sample_db(db_name="sample.db", rows=100000):
    conn = sqlite3.connect(db_name)
    cursor = conn.cursor()
    
    cursor.execute("DROP TABLE IF EXISTS users")
    cursor.execute("""
        CREATE TABLE users (
            id INTEGER PRIMARY KEY,
            name TEXT,
            email TEXT,
            age INTEGER,
            bio TEXT
        )
    """)
    
    data = []
    for i in range(rows):
        name = ''.join(random.choices(string.ascii_letters, k=10))
        email = f"{name}@example.com"
        age = random.randint(18, 80)
        bio = ''.join(random.choices(string.ascii_letters + " ", k=100))
        data.append((i, name, email, age, bio))
    
    cursor.executemany("INSERT INTO users VALUES (?, ?, ?, ?, ?)", data)
    conn.commit()
    conn.close()
    print(f"Created {db_name} with {rows} rows.")

if __name__ == "__main__":
    create_sample_db("Lab 2/sample.db")
