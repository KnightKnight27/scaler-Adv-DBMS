CREATE TABLE books (
  id INTEGER PRIMARY KEY,
  title TEXT NOT NULL,
  author TEXT NOT NULL,
  year_published INTEGER
);

CREATE INDEX idx_books_year ON books(year_published);

INSERT INTO books (title, author, year_published) VALUES
('The Hobbit','J. R. R. Tolkien',1937),
('1984','George Orwell',1949),
('Dune','Frank Herbert',1965),
('Foundation','Isaac Asimov',1951),
('Neuromancer','William Gibson',1984);

VACUUM;
