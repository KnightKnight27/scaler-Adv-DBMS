-- Lab 4 — recipes.db seed
-- 24BCS10123  Kushal Talati
--
-- Forces page_size=8192 so the resulting B-tree topology differs from the
-- 4096-byte default that most reference walkthroughs use. The 1500-byte
-- instructions string is built procedurally to keep this file readable
-- (instead of pasting the long padding 32 times by hand).
PRAGMA page_size = 8192;
PRAGMA journal_mode = DELETE;
BEGIN;

CREATE TABLE cuisines (
  id     INTEGER PRIMARY KEY,
  name   TEXT NOT NULL UNIQUE,
  region TEXT
);

CREATE TABLE recipes (
  id           INTEGER PRIMARY KEY,
  title        TEXT NOT NULL,
  cuisine      TEXT NOT NULL,
  instructions TEXT NOT NULL
);

INSERT INTO cuisines(name, region) VALUES
  ('French',   'Western Europe'),
  ('Indian',   'South Asia'),
  ('Italian',  'Southern Europe'),
  ('Japanese', 'East Asia'),
  ('Lebanese', 'West Asia'),
  ('Mexican',  'North America'),
  ('Spanish',  'Southern Europe'),
  ('Thai',     'Southeast Asia');

-- Each recipe's `instructions` is exactly 1500 bytes of deterministic text
-- so each 8192-byte page ends up holding ~5 recipe rows. The expression
-- below produces a string that begins with a per-recipe lead-in and is
-- padded with a repeating sentence to reach the target length.
WITH base(rn, title, cuisine) AS (VALUES
  ( 1, 'Margherita Pizza',     'Italian'),
  ( 2, 'Carbonara',            'Italian'),
  ( 3, 'Lasagna Bolognese',    'Italian'),
  ( 4, 'Risotto Milanese',     'Italian'),
  ( 5, 'Tiramisu',             'Italian'),
  ( 6, 'Beef Bourguignon',     'French'),
  ( 7, 'Coq au Vin',           'French'),
  ( 8, 'Ratatouille',          'French'),
  ( 9, 'Crepes Suzette',       'French'),
  (10, 'Chicken Tikka Masala', 'Indian'),
  (11, 'Palak Paneer',         'Indian'),
  (12, 'Biryani',              'Indian'),
  (13, 'Masala Dosa',          'Indian'),
  (14, 'Gulab Jamun',          'Indian'),
  (15, 'Sushi Roll',           'Japanese'),
  (16, 'Ramen Shoyu',          'Japanese'),
  (17, 'Okonomiyaki',          'Japanese'),
  (18, 'Tempura',              'Japanese'),
  (19, 'Pad Thai',             'Thai'),
  (20, 'Tom Yum Goong',        'Thai'),
  (21, 'Green Curry',          'Thai'),
  (22, 'Mango Sticky Rice',    'Thai'),
  (23, 'Tacos al Pastor',      'Mexican'),
  (24, 'Mole Poblano',         'Mexican'),
  (25, 'Chiles en Nogada',     'Mexican'),
  (26, 'Churros',              'Mexican'),
  (27, 'Paella Valenciana',    'Spanish'),
  (28, 'Gazpacho',             'Spanish'),
  (29, 'Patatas Bravas',       'Spanish'),
  (30, 'Hummus with Pita',     'Lebanese'),
  (31, 'Tabbouleh',            'Lebanese'),
  (32, 'Shakshuka',            'Lebanese')
)
INSERT INTO recipes(title, cuisine, instructions)
SELECT
  title, cuisine,
  substr(
    'Step-by-step preparation for ' || title || ' (' || cuisine || '). ' ||
    replace(hex(zeroblob(800)),
            '00',
            'Combine the prepared ingredients carefully, adjusting seasoning to taste while keeping the heat steady. '),
    1, 1500)
FROM base
ORDER BY rn;

COMMIT;
