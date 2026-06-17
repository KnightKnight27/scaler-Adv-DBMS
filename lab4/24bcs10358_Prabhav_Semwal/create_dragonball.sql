CREATE TABLE fighters (
    id INTEGER PRIMARY KEY,
    name TEXT,
    race TEXT,
    power_level INTEGER
);

INSERT INTO fighters (name, race, power_level) VALUES
('Goku', 'Saiyan', 9001),
('Vegeta', 'Saiyan', 8500),
('Piccolo', 'Namekian', 7000),
('Frieza', 'Alien', 12000),
('Gohan', 'Saiyan', 8000);

CREATE INDEX idx_fighters_power
ON fighters(power_level);