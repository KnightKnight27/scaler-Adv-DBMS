# Analysis of SQLite and PostgreSQL 

## SQLite3 

### 1. Running Command ls -lh on demo_db Directory with two Database Table Files clubs and movies: 

krisshank-agarwal@krisshank-agarwal-HP-Laptop-15-di0xxx:~/demo_db$ ls -lh
total 20K
-rw-r--r-- 1 krisshank-agarwal krisshank-agarwal  12K May  9 05:26 clubs
-rw-r--r-- 1 krisshank-agarwal krisshank-agarwal 8.0K May  9 05:24 movies 

### 2. Using PRAGMA Command PRAGMA page_size; to find the page size of clubs File: 

krisshank-agarwal@krisshank-agarwal-HP-Laptop-15-di0xxx:~/demo_db$ sqlite3 clubs
SQLite version 3.45.1 2024-01-30 16:01:20
Enter ".help" for usage hints.
sqlite> PRAGMA page_size; 
4096

### 3. Using PRAGMA Command PRAGMA page_count; to find the page count of clubs File: 

sqlite> PRAGMA page_count; 
3

### 4. Using PRAGMA Command PRAGMA mmap_size; to find the mmap size of clubs File: 

sqlite> PRAGMA mmap_size; 
0 

### 5. Changing mmap_size in Clubs File: 

sqlite> PRAGMA mmap_size= 1000000; 
1000000
sqlite> PRAGMA mmap_size; 
1000000 

### 6. Execution time for SELECT * FROM clubs, when mmap_size= 1000000 is: 

sqlite> .timer on
sqlite> PRAGMA mmap_size; 
1000000
Run Time: real 0.000 user 0.000031 sys 0.000046
sqlite> SELECT * FROM clubs; 
1|Alice|India
2|Bob|Canada
Run Time: real 0.000 user 0.000087 sys 0.000111

### 7. Execution time for SELECT * FROM clubs, when mmap_size is 0 is: 

sqlite> PRAGMA mmap_size= 0; 
0
Run Time: real 0.001 user 0.000110 sys 0.000124
sqlite> SELECT * FROM clubs; 
1|Alice|India
2|Bob|Canada
Run Time: real 0.001 user 0.000141 sys 0.000169 

### 8. Running command ps aux | grep sqlite: 

krisshank-agarwal@krisshank-agarwal-HP-Laptop-15-di0xxx:~/demo_db$ ps aux | grep sqlite
krissha+    7786  0.0  0.0   9152  2288 pts/0    S+   04:37   0:00 grep --color=auto sqlite 


## 2. PostgreSQL 

### 1. Using SHOW blocks_size; to see the page size in postgresql_db Database with users Table is: 

krisshank-agarwal@krisshank-agarwal-HP-Laptop-15-di0xxx:~/demo_db$ sudo -u postgres psql
psql (16.13 (Ubuntu 16.13-0ubuntu0.24.04.1))
Type "help" for help.

postgres=# \c postgresql_db
You are now connected to database "postgresql_db" as user "postgres". 
postgresql_db=# SHOW block_size; 
 block_size 
------------
 8192
(1 row) 

### 2. Using pg_relation_size('table-name') and current_setting('block_size') to Calculate page size of users Table: 

postgresql_db=# SELECT pg_relation_size('users' )/ current_setting('block_size' )::bigint AS total_page_size; 
 total_page_size 
-----------------
               1
(1 row)

### 3. Since we cannot disable MMAP in PostgreSQL because it needs some shared memory to run an Instance of its server, so we will show time for executing the query with dynamic_shared_memory_type as posix, and then mmap, POSIX Execution Time is: 

postgresql_db=# SHOW dynamic_shared_memory_type; 
 dynamic_shared_memory_type 
----------------------------
 posix
(1 row)

Time: 0.295 ms
postgresql_db=# SHOW shared_memory_size; 
 shared_memory_size 
--------------------
 16MB
(1 row)

Time: 0.331 ms
postgresql_db=# SELECT * FROM users; 
 id |  name  |      field       
----+--------+------------------
  1 | Tushar | Computer Science
  2 | Samrat | Physics
(2 rows)

Time: 0.516 ms


### 4. Execution Time for Query when dynamic_shared_memory_type Is mmap is: 

postgresql_db=# SHOW dynamic_shared_memory_type;
 dynamic_shared_memory_type 
----------------------------
 mmap
(1 row)

Time: 0.370 ms
postgresql_db=# SELECT * FROM users;
 id |  name  |      field       
----+--------+------------------
  1 | Tushar | Computer Science
  2 | Samrat | Physics
(2 rows)

Time: 0.521 ms

### 5. Using Command ps aux | grep (postgres and postgresql ) in PostgreSQL here: 

krisshank-agarwal@krisshank-agarwal-HP-Laptop-15-di0xxx:~/demo_db$ ps aux | grep postgres
postgres    8769  0.0  0.3  86672 24604 ?        Ss   05:19   0:00 /usr/lib/postgresql/16/bin/postgres -D /var/lib/postgresql/16/main -c config_file=/etc/postgresql/16/main/postgresql.conf
postgres    8770  0.0  0.0  86812  7644 ?        Ss   05:19   0:00 postgres: 16/main: checkpointer 
postgres    8771  0.0  0.0  86804  6556 ?        Ss   05:19   0:00 postgres: 16/main: background writer 
postgres    8773  0.0  0.0  86804  6216 ?        Ss   05:19   0:00 postgres: 16/main: walwriter 
postgres    8774  0.0  0.1  88296  9072 ?        Ss   05:19   0:00 postgres: 16/main: autovacuum launcher 
postgres    8775  0.0  0.1  88264  8200 ?        Ss   05:19   0:00 postgres: 16/main: logical replication launcher 
krissha+    9017  0.0  0.0   9152  2284 pts/0    S+   05:27   0:00 grep --color=auto postgres

krisshank-agarwal@krisshank-agarwal-HP-Laptop-15-di0xxx:~/demo_db$ ps aux | grep postgresql
postgres    8769  0.0  0.3  86672 24604 ?        Ss   05:19   0:00 /usr/lib/postgresql/16/bin/postgres -D /var/lib/postgresql/16/main -c config_file=/etc/postgresql/16/main/postgresql.conf
krissha+    9021  0.0  0.0   9152  2288 pts/0    S+   05:27   0:00 grep --color=auto postgresql 