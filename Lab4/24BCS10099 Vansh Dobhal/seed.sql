-- Lab 4 - recipes.db seed
-- Vansh Dobhal, 24BCS10099
-- Advanced DBMS - Scaler School of Technology
--
-- The database is intentionally small enough to inspect by hand, but each
-- recipe row carries a fixed 950-byte instruction string so the recipes table
-- grows into a multi-page B-tree instead of staying on one leaf page.

PRAGMA page_size = 4096;
PRAGMA journal_mode = DELETE;

BEGIN;

CREATE TABLE cuisines (
  id     INTEGER PRIMARY KEY,
  name   TEXT NOT NULL UNIQUE,
  region TEXT NOT NULL
);

CREATE TABLE recipes (
  id           INTEGER PRIMARY KEY,
  title        TEXT NOT NULL,
  cuisine      TEXT NOT NULL,
  cook_time    INTEGER NOT NULL,
  instructions TEXT NOT NULL
);

INSERT INTO cuisines(name, region) VALUES
  ('Bengali',       'East India'),
  ('French',        'Western Europe'),
  ('Gujarati',      'West India'),
  ('Italian',       'Southern Europe'),
  ('Japanese',      'East Asia'),
  ('Korean',        'East Asia'),
  ('Lebanese',      'West Asia'),
  ('Mexican',       'North America'),
  ('Punjabi',       'North India'),
  ('Thai',          'Southeast Asia');

WITH base(rn, title, cuisine, cook_time) AS (VALUES
  ( 1, 'Aloo Paratha',           'Punjabi',       35),
  ( 2, 'Rajma Chawal',           'Punjabi',       55),
  ( 3, 'Sarson da Saag',         'Punjabi',       70),
  ( 4, 'Dhokla',                 'Gujarati',      40),
  ( 5, 'Undhiyu',                'Gujarati',      80),
  ( 6, 'Thepla',                 'Gujarati',      30),
  ( 7, 'Shorshe Ilish',          'Bengali',       45),
  ( 8, 'Cholar Dal',             'Bengali',       38),
  ( 9, 'Luchi Aloo Dum',         'Bengali',       50),
  (10, 'Margherita Pizza',       'Italian',       42),
  (11, 'Pesto Pasta',            'Italian',       28),
  (12, 'Mushroom Risotto',       'Italian',       48),
  (13, 'Ratatouille',            'French',        60),
  (14, 'Onion Soup',             'French',        52),
  (15, 'Crepes Suzette',         'French',        34),
  (16, 'Vegetable Ramen',        'Japanese',      44),
  (17, 'Miso Soup',              'Japanese',      25),
  (18, 'Okonomiyaki',            'Japanese',      36),
  (19, 'Kimchi Fried Rice',      'Korean',        32),
  (20, 'Bibimbap',               'Korean',        46),
  (21, 'Tteokbokki',             'Korean',        33),
  (22, 'Pad Thai',               'Thai',          30),
  (23, 'Green Curry',            'Thai',          45),
  (24, 'Tom Yum Soup',           'Thai',          37),
  (25, 'Tacos al Pastor',        'Mexican',       50),
  (26, 'Black Bean Enchiladas',  'Mexican',       58),
  (27, 'Elote',                  'Mexican',       22),
  (28, 'Hummus Bowl',            'Lebanese',      26),
  (29, 'Falafel Wrap',           'Lebanese',      41),
  (30, 'Tabbouleh',              'Lebanese',      20)
)
INSERT INTO recipes(title, cuisine, cook_time, instructions)
SELECT
  title,
  cuisine,
  cook_time,
  substr(
    'Recipe notes for ' || title || ' from the ' || cuisine || ' kitchen. ' ||
    replace(hex(zeroblob(520)),
            '00',
            'Measure, prep, cook, taste, and plate with steady heat and clean timing. '),
    1,
    950
  )
FROM base
ORDER BY rn;

COMMIT;