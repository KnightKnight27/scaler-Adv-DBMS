PRAGMA foreign_keys = ON;

-- Rebuild the experiment from scratch so repeated runs stay deterministic.
DROP INDEX IF EXISTS idx_registrations_learner_term;
DROP INDEX IF EXISTS idx_modules_division;
DROP TABLE IF EXISTS registrations;
DROP TABLE IF EXISTS modules;
DROP TABLE IF EXISTS learners;
DROP TABLE IF EXISTS divisions;

BEGIN TRANSACTION;

-- Academic divisions act as the parent table for both learners and modules.
CREATE TABLE divisions (
    division_id INTEGER PRIMARY KEY,
    division_name TEXT NOT NULL UNIQUE,
    home_block TEXT NOT NULL,
    office_extension TEXT NOT NULL
);

-- Learners belong to one division and represent enrolled students on campus.
CREATE TABLE learners (
    learner_id INTEGER PRIMARY KEY,
    full_name TEXT NOT NULL,
    division_id INTEGER NOT NULL,
    semester INTEGER NOT NULL CHECK (semester BETWEEN 1 AND 8),
    residence TEXT NOT NULL,
    FOREIGN KEY (division_id) REFERENCES divisions(division_id)
);

-- Modules are owned by a division but may be taken by learners across campus.
CREATE TABLE modules (
    module_code TEXT PRIMARY KEY,
    module_title TEXT NOT NULL,
    credits INTEGER NOT NULL CHECK (credits BETWEEN 2 AND 5),
    division_id INTEGER NOT NULL,
    FOREIGN KEY (division_id) REFERENCES divisions(division_id)
);

-- Registrations connect learners to modules for a given academic term.
CREATE TABLE registrations (
    registration_id INTEGER PRIMARY KEY,
    learner_id INTEGER NOT NULL,
    module_code TEXT NOT NULL,
    term_label TEXT NOT NULL,
    grade TEXT,
    FOREIGN KEY (learner_id) REFERENCES learners(learner_id),
    FOREIGN KEY (module_code) REFERENCES modules(module_code),
    UNIQUE (learner_id, module_code, term_label)
);

CREATE INDEX idx_registrations_learner_term
    ON registrations(learner_id, term_label);

CREATE INDEX idx_modules_division
    ON modules(division_id);

INSERT INTO divisions (division_id, division_name, home_block, office_extension) VALUES
    (1, 'Computing Systems', 'North Tower', '2201'),
    (2, 'Design and Media', 'Studio Wing', '2314'),
    (3, 'Commerce Analytics', 'Lake Hall', '2420'),
    (4, 'Life Science Research', 'Discovery Annex', '2508');

INSERT INTO learners (learner_id, full_name, division_id, semester, residence) VALUES
    (1001, 'Aditi Rao', 1, 4, 'Hostel C-12'),
    (1002, 'Neel Dutta', 1, 6, 'Hostel A-08'),
    (1003, 'Riya Sen', 2, 2, 'Hostel E-03'),
    (1004, 'Kabir Mehta', 3, 5, 'Day Scholar'),
    (1005, 'Mitali Ghosh', 4, 3, 'Hostel B-14'),
    (1006, 'Arjun Sethi', 1, 1, 'Hostel D-09'),
    (1007, 'Tanya Joseph', 3, 7, 'Hostel F-01'),
    (1008, 'Samarjit Paul', 2, 4, 'Day Scholar');

INSERT INTO modules (module_code, module_title, credits, division_id) VALUES
    ('CS241', 'Database Internals', 4, 1),
    ('CS255', 'Distributed Data Systems', 3, 1),
    ('DM210', 'Interface Storyboarding', 3, 2),
    ('DM225', 'Visual Analytics Studio', 4, 2),
    ('CA310', 'Financial Reporting Lab', 3, 3),
    ('CA322', 'Business Forecasting', 4, 3),
    ('LS205', 'Cell Signalling', 4, 4),
    ('LS218', 'Bioinformatics Foundations', 3, 4);

INSERT INTO registrations (registration_id, learner_id, module_code, term_label, grade) VALUES
    (1, 1001, 'CS241', '2025-Odd', 'A'),
    (2, 1001, 'CS255', '2025-Odd', 'A-'),
    (3, 1002, 'CS241', '2025-Odd', 'B+'),
    (4, 1002, 'CA322', '2025-Odd', 'A'),
    (5, 1003, 'DM210', '2025-Odd', 'A'),
    (6, 1003, 'DM225', '2025-Odd', 'B'),
    (7, 1004, 'CA310', '2025-Odd', 'A-'),
    (8, 1004, 'CA322', '2025-Odd', 'B+'),
    (9, 1005, 'LS205', '2025-Odd', 'A'),
    (10, 1005, 'LS218', '2025-Odd', 'A-'),
    (11, 1006, 'CS241', '2025-Odd', NULL),
    (12, 1006, 'DM225', '2025-Odd', NULL),
    (13, 1007, 'CA322', '2025-Odd', 'A'),
    (14, 1007, 'CS255', '2025-Odd', 'B+'),
    (15, 1008, 'DM210', '2025-Odd', 'B+'),
    (16, 1008, 'LS218', '2025-Odd', 'A-');

COMMIT;

-- Verification queries for a quick sanity check after loading the script.
SELECT 'Division Count' AS check_name, COUNT(*) AS total_rows
FROM divisions;

SELECT l.learner_id,
       l.full_name,
       d.division_name,
       l.semester,
       l.residence
FROM learners AS l
JOIN divisions AS d
  ON d.division_id = l.division_id
ORDER BY l.learner_id;

SELECT r.registration_id,
       l.full_name,
       r.module_code,
       m.module_title,
       r.term_label,
       COALESCE(r.grade, 'IP') AS grade_status
FROM registrations AS r
JOIN learners AS l
  ON l.learner_id = r.learner_id
JOIN modules AS m
  ON m.module_code = r.module_code
ORDER BY r.registration_id;

SELECT d.division_name,
       COUNT(m.module_code) AS module_count
FROM divisions AS d
LEFT JOIN modules AS m
  ON m.division_id = d.division_id
GROUP BY d.division_id
ORDER BY d.division_id;
