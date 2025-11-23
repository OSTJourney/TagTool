#include "../includes/Database.hpp"

#include <cmath>
#include <iostream>
#include <fstream>
#include <random>
#include <thread>

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

	sqlite3_busy_timeout(_db, 5000); // Set busy timeout to 5000 ms

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
	if (!_db)
		return;
	
	if (inTransaction())
		commitTransaction();

	if (_stmtInsertSong)
		sqlite3_finalize(_stmtInsertSong);
	if (_stmtUpdateSong)
		sqlite3_finalize(_stmtUpdateSong);
	if (_stmtGetSongById)
		sqlite3_finalize(_stmtGetSongById);
	
	sqlite3_close(_db);

	_stmtInsertSong = nullptr;
	_stmtUpdateSong = nullptr;
	_stmtGetSongById = nullptr;

	_db = nullptr;
}

bool Database::inTransaction()
{
	sqlite3_stmt *stmt = nullptr;
	if (sqlite3_prepare_v2(_db, "PRAGMA in_transaction;", -1, &stmt, nullptr) != SQLITE_OK)
		return (false);

	int in = 0;
	if (sqlite3_step(stmt) == SQLITE_ROW)
		in = sqlite3_column_int(stmt, 0);

	sqlite3_finalize(stmt);
	return (in != 0);
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

#include "../includes/Utils.hpp"

bool Database::upsertSong(const s_songRecord &song, bool isNew)
{
	log("Upserting song ID " + song.id + " (" + (isNew ? "new" : "update") + ")", false);
	sqlite3_stmt	*stmt	= nullptr;
	int				idx		= 1;

	if (isNew)
	{
		// Prepare insert statement once
		if (!_stmtInsertSong)
		{
			const std::string ins =
				"INSERT INTO songs (id,title,artist,album,cover,duration,tags,path) "
				"VALUES (?,?,?,?,?,?,?,?);";
			if (sqlite3_prepare_v2(_db, ins.c_str(), -1, &_stmtInsertSong, nullptr) != SQLITE_OK)
				throw std::runtime_error("Failed to prepare insert statement");
		}
		stmt = _stmtInsertSong;
	}
	else
	{
		// Prepare update statement once
		if (!_stmtUpdateSong)
		{
			const std::string upd =
				"UPDATE songs SET title=?, artist=?, album=?, cover=?, duration=?, tags=?, path=? WHERE id=?;";
			if (sqlite3_prepare_v2(_db, upd.c_str(), -1, &_stmtUpdateSong, nullptr) != SQLITE_OK)
				throw std::runtime_error("Failed to prepare update statement");
		}
		stmt = _stmtUpdateSong;
	}

	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);

	if (isNew)
		sqlite3_bind_text(stmt, idx++, song.id.c_str(), -1, SQLITE_TRANSIENT);

	sqlite3_bind_text(stmt, idx++, song.title.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, idx++, song.artist.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, idx++, song.album.c_str(), -1, SQLITE_TRANSIENT);

	if (song.cover)
		sqlite3_bind_int(stmt, idx++, *song.cover);
	else sqlite3_bind_null(stmt, idx++);

	sqlite3_bind_double(stmt, idx++, song.duration);
	sqlite3_bind_text(stmt, idx++, song.tags.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, idx++, song.path.c_str(), -1, SQLITE_TRANSIENT);

	if (!isNew) // For update, bind id at the end
		sqlite3_bind_text(stmt, idx++, song.id.c_str(), -1, SQLITE_TRANSIENT);

	int	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		std::cerr << "sqlite3_step() failed for id=" << song.id
				  << " rc=" << rc << " err=" << sqlite3_errmsg(_db) << "\n";
		return (false);
	}
	return (true);

}

std::vector<s_songRecord> Database::fetchSongsWithNullCover()
{
	std::vector<s_songRecord> result;
	const std::string sql = "SELECT id,title,artist,album,cover,duration,tags,path FROM songs WHERE cover IS NULL;";
	if (auto s = prepare(sql)) {
		while (sqlite3_step(*s) == SQLITE_ROW) {
			s_songRecord rec;
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

bool Database::inserts_logAddition(const s_logAddition &log)
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
	static thread_local std::mt19937	rng(std::random_device{}());	// Random number generator for jitter
	
	for (int i = 0; i < 5; i++)
	{
		if (execute("COMMIT;"))
			return (true);

		// Exponential backoff with jitter
		int	baseDelay = 50 * (1 << i);
		std::uniform_int_distribution<int> dist(0, 30); // 0 to 30ms of jitter

		int	delay = baseDelay + dist(rng);
		std::this_thread::sleep_for(std::chrono::milliseconds(delay));
	}

	std::cerr << "DB COMMIT failed after retries\n";
	return (false);
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

s_songRecord Database::getSongById(const std::string &id) {
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
		throw RecordNotFound();
	if (rc != SQLITE_ROW)
		throw std::runtime_error("Unexpected result in getSongById");

	s_songRecord	song;	// Temporary variable to hold the song record
	song.id	  = reinterpret_cast<const char*>(sqlite3_column_text(_stmtGetSongById, 0));
	song.title   = reinterpret_cast<const char*>(sqlite3_column_text(_stmtGetSongById, 1));
	song.artist  = reinterpret_cast<const char*>(sqlite3_column_text(_stmtGetSongById, 2));
	song.album   = reinterpret_cast<const char*>(sqlite3_column_text(_stmtGetSongById, 3));

	if (sqlite3_column_type(_stmtGetSongById, 4) == SQLITE_NULL)
		song.cover = std::nullopt;
	else
		song.cover = sqlite3_column_int(_stmtGetSongById, 4);

	song.duration = sqlite3_column_double(_stmtGetSongById, 5);
	song.tags	 = reinterpret_cast<const char*>(sqlite3_column_text(_stmtGetSongById, 6));
	song.path	 = reinterpret_cast<const char*>(sqlite3_column_text(_stmtGetSongById, 7));

	return (song);
}

s_songRecord Database::getSongById(const int id) {
	return getSongById(std::to_string(id));
}
