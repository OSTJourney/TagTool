CREATE TABLE IF NOT EXISTS songs (
	id			TEXT	PRIMARY KEY,
	title		TEXT	NOT NULL	DEFAULT 'Unknown Title',
	artist		TEXT	NOT NULL	DEFAULT 'Unknown Artist',
	album		TEXT	NOT NULL	DEFAULT 'Unknown Album',
	cover		INTEGER				DEFAULT NULL,
	duration	REAL	NOT NULL	DEFAULT 0,
	tags		TEXT	NOT NULL	DEFAULT '{}',
	path		TEXT	NOT NULL	DEFAULT ''
);

CREATE TABLE IF NOT EXISTS log_additions (
	id			INTEGER	PRIMARY KEY	AUTOINCREMENT,
	year		INTEGER,
	month		INTEGER,
	day			INTEGER,
	first_id	INTEGER,
	last_id		INTEGER,
	comment		TEXT
);
