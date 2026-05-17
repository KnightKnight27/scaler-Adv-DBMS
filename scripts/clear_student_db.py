import sqlite3, os
path = os.path.join('lab3','24bcs10276','student.db')
if not os.path.exists(path):
    print('student.db not found')
    raise SystemExit(1)
conn = sqlite3.connect(path)
c = conn.cursor()
# Delete all rows
c.execute('DELETE FROM students')
# Vacuum to reduce file size
conn.commit()
conn.execute('VACUUM')
conn.close()
print('cleared', path, 'size', os.path.getsize(path))
