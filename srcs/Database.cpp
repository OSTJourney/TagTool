#include "../includes/Database.hpp"

#include <iostream>
#include <fstream>

Database::Database(const std::string &filename)
	: _path(filename) {}

Database::~Database()
{
	close();
}

bool Database::open()
{
	if (sqlite3_open(_path.c_str(), &_db) != SQLITE_OK)
		return (false);

	// Enable WAL mode for high concurrency
	execute("PRAGMA journal_mode=WAL;");
	execute("PRAGMA synchronous=NORMAL;");

	// Enable large memory cache (256 MB)
	execute("PRAGMA cache_size = -262144;");

	// Avoid disk spills for max performance
	execute("PRAGMA cache_spill = OFF;");

	return (true);
}

void Database::close()
{
	if (_db)
		sqlite3_close(_db);
	_db = nullptr;
}

bool Database::execute(const std::string &sql)
{
	char	*err = nullptr;	// error message pointer
	if (sqlite3_exec(_db, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
		std::cerr << "SQL error: " << err << std::endl;
		sqlite3_free(err);
		return (false);
	}
	return (true);
}

std::optional<sqlite3_stmt*> Database::prepare(const std::string &sql)
{
	sqlite3_stmt	*stmt = nullptr;	// prepared statement pointer
	if (sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK)
		return (stmt);

	std::cerr << "Failed to prepare: " << sql << std::endl;
	return (std::nullopt);
}

bool Database::initSchema()
{
	std::ifstream	schemaFile("schema.sql");	// open schema file
	if (!schemaFile.is_open())
		return (false);

	std::string	sql; 	// SQL commands
	std::string	line;	// current line

	while (std::getline(schemaFile, line))
		sql += line + "\n";

	return (execute(sql));
}

bool Database::upsertSong(
	const SongRecord	&song,
	bool				&isNew)
{
	// Try update first
	const std::string	upd =
		"UPDATE songs SET title=?, artist=?, album=?, cover=?, duration=?, tags=?, path=? WHERE id=?;";
	if (auto s = prepare(upd)) {
		sqlite3_bind_text(*s, 1, song.title.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(*s, 2, song.artist.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(*s, 3, song.album.c_str(), -1, SQLITE_TRANSIENT);
		if (song.cover)
			sqlite3_bind_int(*s, 4, *song.cover);
		else
			sqlite3_bind_null(*s, 4);

		sqlite3_bind_double(*s, 5, song.duration);
		sqlite3_bind_text(*s, 6, song.tags.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(*s, 7, song.path.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(*s, 8, song.id.c_str(), -1, SQLITE_TRANSIENT);

		if (sqlite3_step(*s) == SQLITE_DONE && sqlite3_changes(_db) > 0) {
			sqlite3_finalize(*s);
			isNew = false;
			return (true);
		}
		sqlite3_finalize(*s);
	} else
		return (false);
	// Insert
	const std::string ins =
		"INSERT INTO songs (id,title,artist,album,cover,duration,tags,path) VALUES(?,?,?,?,?,?,?,?);";
	if (auto s2 = prepare(ins)) {
		sqlite3_bind_text(*s2, 1, song.id.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(*s2, 2, song.title.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(*s2, 3, song.artist.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(*s2, 4, song.album.c_str(), -1, SQLITE_TRANSIENT);
		if (song.cover)
			sqlite3_bind_int(*s2, 5, *song.cover);
		else
			sqlite3_bind_null(*s2, 5);

		sqlite3_bind_double(*s2, 6, song.duration);
		sqlite3_bind_text(*s2, 7, song.tags.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(*s2, 8, song.path.c_str(), -1, SQLITE_TRANSIENT);
		bool ok = sqlite3_step(*s2) == SQLITE_DONE; // Is insert successful
		sqlite3_finalize(*s2);
		isNew = true;
		return (ok);
	}
	return (false);
}

std::vector<SongRecord> Database::fetchSongsWithNullCover()
{
	std::vector<SongRecord> result;
	const std::string sql = "SELECT id,title,artist,album,cover,duration,tags,path FROM songs WHERE cover IS NULL;";
	if (auto s = prepare(sql)) {
		while (sqlite3_step(*s) == SQLITE_ROW) {
			SongRecord rec;
			rec.id		= reinterpret_cast<const char*>(sqlite3_column_text(*s, 0));
			rec.title	= reinterpret_cast<const char*>(sqlite3_column_text(*s, 1));
			rec.artist	= reinterpret_cast<const char*>(sqlite3_column_text(*s, 2));
			rec.album	= reinterpret_cast<const char*>(sqlite3_column_text(*s, 3));
			if (sqlite3_column_type(*s, 4) != SQLITE_NULL)
				rec.cover = sqlite3_column_int(*s, 4);
			rec.duration = sqlite3_column_double(*s, 5);
			rec.tags	= reinterpret_cast<const char*>(sqlite3_column_text(*s, 6));
			rec.path	= reinterpret_cast<const char*>(sqlite3_column_text(*s, 7));
			result.push_back(std::move(rec));
		}
		sqlite3_finalize(*s);
	}
	return (result);
}

bool Database::insertLogAddition(const LogAddition &log)
{
	const std::string ins =
		"INSERT INTO log_additions (year,month,day,first_id,last_id,comment) VALUES(?,?,?,?,?,?);";
	if (auto s = prepare(ins)) {
		sqlite3_bind_int(*s, 1, log.year);
		sqlite3_bind_int(*s, 2, log.month);
		sqlite3_bind_int(*s, 3, log.day);
		sqlite3_bind_int(*s, 4, log.first_id);
		sqlite3_bind_int(*s, 5, log.last_id);
		sqlite3_bind_text(*s, 6, log.comment.c_str(), -1, SQLITE_TRANSIENT);
		bool ok = sqlite3_step(*s) == SQLITE_DONE;
		sqlite3_finalize(*s);
		return (ok);
	}
	return (false);
}

bool Database::beginTransaction()
{
	return (execute("BEGIN TRANSACTION;"));
}

bool Database::commitTransaction()
{
	return (execute("COMMIT;"));
}

unsigned int Database::getLastSongId()
{
	const	std::string sql = "SELECT MAX(CAST(id AS INTEGER)) FROM songs;"; // Sql statement to get max id
	if (auto s = prepare(sql)) {
		if (sqlite3_step(*s) == SQLITE_ROW) {
			int	lastId = sqlite3_column_int(*s, 0); // Get the max id
			sqlite3_finalize(*s);
			return (static_cast<unsigned int>(lastId));
		}
		sqlite3_finalize(*s);
	}
	return (0);
}

SongRecord Database::getSongById(const std::string &id) {
	if (!_db)
		throw std::runtime_error("Database not opened");

	if (_stmtGetSongById == nullptr) {
		const char	*sql =
			"SELECT id, title, artist, album, cover, duration, tags, path "
			"FROM songs WHERE id = ? LIMIT 1;";

		if (sqlite3_prepare_v2(_db, sql, -1, &_stmtGetSongById, nullptr) != SQLITE_OK)
			throw std::runtime_error("Failed to prepare statement getSongById");
	}

	sqlite3_reset(_stmtGetSongById);
	sqlite3_clear_bindings(_stmtGetSongById);

	if (sqlite3_bind_text(_stmtGetSongById, 1, id.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
		throw std::runtime_error("Failed to bind id (string) in getSongById");

	int rc = sqlite3_step(_stmtGetSongById);
	if (rc == SQLITE_DONE)
		throw std::runtime_error("Song not found: " + id);
	if (rc != SQLITE_ROW)
		throw std::runtime_error("Unexpected result in getSongById");

	SongRecord	song;	// Temporary variable to hold the song record
	song.id      = reinterpret_cast<const char*>(sqlite3_column_text(_stmtGetSongById, 0));
	song.title   = reinterpret_cast<const char*>(sqlite3_column_text(_stmtGetSongById, 1));
	song.artist  = reinterpret_cast<const char*>(sqlite3_column_text(_stmtGetSongById, 2));
	song.album   = reinterpret_cast<const char*>(sqlite3_column_text(_stmtGetSongById, 3));

	if (sqlite3_column_type(_stmtGetSongById, 4) == SQLITE_NULL)
		song.cover = std::nullopt;
	else
		song.cover = sqlite3_column_int(_stmtGetSongById, 4);

	song.duration = sqlite3_column_double(_stmtGetSongById, 5);
	song.tags     = reinterpret_cast<const char*>(sqlite3_column_text(_stmtGetSongById, 6));
	song.path     = reinterpret_cast<const char*>(sqlite3_column_text(_stmtGetSongById, 7));

	return (song);
}

SongRecord Database::getSongById(const int id) {
	return getSongById(std::to_string(id));
}
