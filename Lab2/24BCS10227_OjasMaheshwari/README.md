
# Creating the DB (Process almost same for both sqlite and psql)

- `sqlite3 schooldb`

- CREATE TABLE students (
  roll_no INT PRIMARY KEY,
  fullname TEXT NOT NULL,
  age INT NOT NULL,
  grade TEXT NOT NULL
);

- INSERT INTO students VALUES (1, "Ojas Maheshwari", 19, "A+");

# Inspecting file size in sqlite

- `ls -lh`
total 8.0K
-rw-r--r-- 1 epicman epicman 8.0K May  8 20:28 schooldb

# Page size and number of pages in sqlite3

- sqlite> PRAGMA page_size;
4096

- sqlite> PRAGMA page_count;
2
<br>
This makes sense because the file size is also 8.0K which is equivalent to 2 pages.

# Page size in PostgresQL
SELECT current_setting('block_size');
8192 (Almost double of sqlite3 ???)

# mmap_size in sqlite3

- sqlite> PRAGMA mmap_size;
0

# Does changing mmap_size make a difference? sqlite3
Yes, theoretically it should. Because then data is not being copied from kernel buffer cache to user space. It's being memory mapped. <br>
I tested this hypothesis on a employeedb from the internet that had data for 500,000 employees.

With mmap_size = 0,
Run Time: real 0.466 user 0.138985 sys 0.321491
With mmap_size = 268435456 (arbitrarily large so all content is memory mapped)
Run Time: real 0.450 user 0.145599 sys 0.290271

The time seems to be decreased.

# mmap_size in PostgresQL
I couldn't find the exact terminology "mmap_size" in postgresQL.<br>
I am guessing it might be related to the size of "shared memory" ?
According to official documentation https://www.postgresql.org/docs/current/runtime-config-resource.html, the size of shared memory is 128 MB by default.

# sqlite runs as a library and postgres runs as a service - confirmed via `ps aux`

┌──(epicman㉿kali)-[~]
└─$ ps aux | grep sqlite3
epicman    71776  0.0  0.0   7152  2180 pts/2    S+   23:11   0:00 grep --color=auto sqlite3
                                                                                                                                                                              
┌──(epicman㉿kali)-[~]
└─$ ps aux | grep postgres
postgres   68910  0.0  0.1 218296 29732 ?        Ss   23:02   0:00 /usr/lib/postgresql/17/bin/postgres -D /var/lib/postgresql/17/main -c config_file=/etc/postgresql/17/main/postgresql.conf
postgres   68911  0.0  0.1 218580 21688 ?        Ss   23:02   0:00 postgres: 17/main: checkpointer 
postgres   68912  0.0  0.0 218432  7448 ?        Ss   23:02   0:00 postgres: 17/main: background writer 
postgres   68914  0.0  0.0 218296 10304 ?        Ss   23:02   0:00 postgres: 17/main: walwriter 
postgres   68915  0.0  0.0 219884  8856 ?        Ss   23:02   0:00 postgres: 17/main: autovacuum launcher 
postgres   68916  0.0  0.0 219860  8088 ?        Ss   23:02   0:00 postgres: 17/main: logical replication launcher 
root       70585  0.0  0.0  20712  7484 pts/3    S+   23:06   0:00 sudo su - postgres
root       70587  0.0  0.0  20712  2628 pts/6    Ss   23:06   0:00 sudo su - postgres
root       70588  0.0  0.0  11408  4784 pts/6    S    23:06   0:00 su - postgres
postgres   70590  0.0  0.0   9120  5164 pts/6    S    23:06   0:00 -bash
postgres   70721  0.0  0.0  19904  7880 pts/6    S+   23:07   0:00 /usr/lib/postgresql/18/bin/psql -s schooldb
postgres   70723  0.0  0.1 221484 21228 ?        Ss   23:07   0:00 postgres: 17/main: postgres schooldb [local] idle
epicman    71805  0.0  0.0   7152  2176 pts/2    S+   23:11   0:00 grep --color=auto postgres


