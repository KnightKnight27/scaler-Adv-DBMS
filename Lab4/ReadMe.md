# Lab 4

I created a SQLite database file and checked it with `xxd`.

## What I did

1. Created a database file named `inventory.db`.
2. Added one table:

    ```sql
    CREATE TABLE products(id INTEGER PRIMARY KEY, item_name TEXT);
    ```

3. Added a few rows:

    ```sql
    INSERT INTO products(item_name) VALUES ('laptop'), ('mouse'), ('monitor');
    ```

4. Checked the file in hex with `xxd`.
5. Checked the SQLite page size, page count, and schema.

## What I found

- The file starts with `SQLite format 3`, so it is a valid SQLite database.
- The page size is `4096` bytes.
- The page count is `2`.
- The table `products` has root page `2`.
- Page `2` starts with `0d`, which is a table leaf B-tree page.
- The page header also shows 3 cells, which matches the 3 rows I inserted.

## Hex bytes from the table page

```text
00001000: 0d 00 00 00 03 0f e2 00 0f f4 0f eb 0f e2 00 00