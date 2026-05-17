import sqlite3
import random
import string
import os

def generate_database(db_filename="test_db.sqlite", num_records=150000):
    # Ensure directory exists if needed, though we run it in the right dir
    conn = sqlite3.connect(db_filename)
    cur = conn.cursor()
    
    cur.execute("DROP TABLE IF EXISTS employees")
    cur.execute("""
        CREATE TABLE employees (
            emp_id INTEGER PRIMARY KEY,
            full_name TEXT,
            contact_email TEXT,
            years_experience INTEGER,
            department TEXT
        )
    """)
    
    records = []
    departments = ['HR', 'Engineering', 'Sales', 'Marketing', 'Finance']
    for i in range(num_records):
        name_len = random.randint(8, 15)
        name = ''.join(random.choices(string.ascii_lowercase, k=name_len)).capitalize()
        email = f"{name.lower()}@company.com"
        exp = random.randint(1, 40)
        dept = random.choice(departments)
        records.append((i, name, email, exp, dept))
    
    cur.executemany("INSERT INTO employees VALUES (?, ?, ?, ?, ?)", records)
    conn.commit()
    conn.close()
    print(f"Successfully generated {db_filename} containing {num_records} records.")

if __name__ == "__main__":
    generate_database("Lab 2/test_db.sqlite")
