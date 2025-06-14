#ifndef DATABASE_HPP
#define DATABASE_HPP

#include <string>
#include <vector>
#include <sqlite3.h>
#include <optional>

struct SongRecord {
	std::string id;
	std::string title;
	std::string artist;
	std::string album;
	std::optional<int> cover;
	double duration;
	std::string tags;
	std::string path;
};

struct LogAddition {
	int id;
	int year;
	int month;
	int day;
	int first_id;
	int last_id;
	std::string comment;
};

class Database {
	public:
		explicit Database(const std::string &filename);
		~Database();

		bool open();
		void close();

		bool initSchema();

		bool upsertSong(const SongRecord &song, bool &isNew);
		std::vector<SongRecord> fetchSongsWithNullCover();

		bool insertLogAddition(const LogAddition &log);

		bool beginTransaction();
		bool commitTransaction();
	private:
		sqlite3 *_db = nullptr;
		std::string _path;

		bool execute(const std::string &sql);
		std::optional<sqlite3_stmt*> prepare(const std::string &sql);
};

#endif
