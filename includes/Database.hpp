#ifndef DATABASE_HPP
#define DATABASE_HPP

#include <string>
#include <vector>
#include <sqlite3.h>
#include <optional>

/**
 * @brief Structure representing a song record in the database.
 * @details Contains:
 * 	- id:		Unique identifier for the song (42id).
 * 	- title:	Song title.
 * 	- artist:	Artist name.
 * 	- album:	Album name.
 * 	- cover:	Optional cover image ID.
 * 	- duration:	Duration of the song in seconds.
 * 	- tags:		Tags associated with the song.
 * 	- path:		File path to the song.
 */
struct s_songRecord {
	std::string			id;			// Unique identifier for the song (42id)
	std::string			title;		// Song title
	std::string			artist;		// Artist name
	std::string			album;		// Album name
	std::optional<int>	cover;		// Optional cover image ID
	double				duration;	// Duration of the song in seconds
	std::string			tags;		// Tags associated with the song
	std::string			path;		// File path to the song
};

/**
 * @brief Structure representing a log entry for additions to the database.
 * @details Contains:
 * 	- id:		Unique identifier for the log entry.
 * 	- year:		Year of the addition.
 * 	- month:	Month of the addition.
 * 	- day:		Day of the addition.
 * 	- first_id:	ID of the first song added.
 * 	- last_id:	ID of the last song added.
 * 	- comment:	Comment about the addition.
 */
struct s_logAddition {
	int			id;			// Unique identifier for the log entry
	int			year;		// Year of the addition
	int			month;		// Month of the addition
	int			day;		// Day of the addition
	int			first_id;	// ID of the first song added
	int			last_id;	// ID of the last song added
	std::string comment;	// Comment about the addition
};

/**
 * @brief Class for managing the SQLite database.
 *
 * This class provides methods to open, close, and interact with the database,
 * including inserting and fetching song records and log additions.
 */
class Database {
	public:
		/**
		 * @brief Construct a new Database object.
		 * @param filename The path to the SQLite database file.
		 */
		explicit Database(const std::string &filename);

		/**
		 * @brief Destroy the Database object and close the database connection.
		 */
		~Database();

		/**
		 * @brief Open the database connection.
		 * @return true on success,
		 * @return false on failure.
		 */
		bool	open();
		/**
		 * @brief Close the database connection.
		 */
		void	close();

		/**
		 * @brief Check if a transaction is currently active.
		 * @return true if in a transaction,
		 * @return false otherwise.
		 */
		bool	inTransaction();

		/**
		 * @brief Initialize the database schema.
		 * Reads the schema from @file `schema.sql` file and executes it.
		 * Creates necessary tables if they do not exist:
		 * 	- `songs`
		 * 	- `log_additions`
		 * @return true on success,
		 * @return false on failure.
		 */
		bool	initSchema();

		/**
		 * @brief Insert or update a song record in the database.
		 * @param song The song record to insert or update.
		 * @param isNew Indicates if the record is new, @true for insert, @false for update.
		 * @return true on success,
		 * @return false on failure.
		 */
		bool	upsertSong(const s_songRecord &song, bool isNew);
		/**
		 * @brief Fetch all song records with a null cover field.
		 * @return A vector of s_songRecord objects with null cover.
		 */
		std::vector<s_songRecord>	fetchSongsWithNullCover();

		/**
		 * @brief Insert a log addition entry into the database.
		 * @param log The log addition entry to insert.
		 * @return true on success,
		 * @return false on failure.
		 */
		bool	inserts_logAddition(const s_logAddition &log);

		/**
		 * @brief Begin a transaction.
		 * @return true on success,
		 * @return false on failure.
		 */
		bool	beginTransaction();
		/**
		 * @brief Commit the current transaction.
		 * @return true on success,
		 * @return false on failure.
		 */
		bool	commitTransaction();

		/**
		 * @brief Get the last inserted song ID.
		 * @return The last inserted song ID.
		 */
		unsigned int	getLastSongId();
		/**
		 * @brief Get the Song By Id
		 * @param id The ID of the song (string)
		 * @return s_songRecord 
		 */
		s_songRecord		getSongById(const std::string	&id);
		/**
		 * @brief Get the Song By Id
		 * @param id The ID of the song (integer)
		 * @return s_songRecord 
		 */
		s_songRecord		getSongById(const int			id);

		/**
		 * @brief Exception thrown when a record is not found in the database.
		 */
		class RecordNotFound : public std::exception
		{
		public:
			const char *what() const noexcept override
			{
				return ("record not found");
			}
		};
	private:
		sqlite3			*_db = nullptr;				// SQLite database connection
		std::string		_path;						// Path to the database file

		// Prepared statements
		sqlite3_stmt*	_stmtInsertSong = nullptr;	// Prepared statement for inserting a song
		sqlite3_stmt*	_stmtUpdateSong = nullptr;	// Prepared statement for updating a song
		sqlite3_stmt*	_stmtGetSongById = nullptr; // Prepared statement for getting song by ID

		/**
		 * @brief Execute a SQL statement.
		 * @param sql The SQL statement to execute.
		 * @return true on success,
		 * @return false on failure.
		 */
		bool execute(const std::string &sql);
		/**
		 * @brief Prepare a SQL statement.
		 * @param sql The SQL statement to prepare.
		 * @return	An optional containing the prepared statement on success,
		 * 			or std::nullopt on failure.
		 */
		std::optional<sqlite3_stmt*> prepare(const std::string &sql);
};

#endif
