CREATE TABLE IF NOT EXISTS songs (
	id TEXT PRIMARY KEY,
	title TEXT,
	artist TEXT,
	album TEXT,
	cover INTEGER DEFAULT NULL,
	duration REAL,
	tags TEXT,
	path TEXT
);

CREATE TABLE IF NOT EXISTS log_additions (
	id INTEGER PRIMARY KEY AUTOINCREMENT,
	year INTEGER,
	month INTEGER,
	day INTEGER,
	first_id INTEGER,
	last_id INTEGER,
	comment TEXT
);
