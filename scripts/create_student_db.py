import sqlite3, os
path = os.path.join('lab3','24bcs10276','student.db')
os.makedirs(os.path.dirname(path), exist_ok=True)
conn = sqlite3.connect(path)
c = conn.cursor()
c.execute('''CREATE TABLE IF NOT EXISTS students (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    roll TEXT NOT NULL UNIQUE,
    name TEXT NOT NULL,
    department TEXT
)''')
students = [
    ('24BCS10276','Sahadheep','CSE'),
    ('24BCS10277','Alex','CSE'),
    ('24BCS10278','Jordan','IT')
]
for roll,name,dept in students:
    c.execute('INSERT OR IGNORE INTO students (roll,name,department) VALUES (?,?,?)',(roll,name,dept))
conn.commit()
conn.close()
print('created', path, 'size', os.path.getsize(path))
