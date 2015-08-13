#include "include/db.h"
#include "include/debug.h"
#include "include/filesystem.h"
#include "include/sha.h"

#include <cstring>

using std::string;

// db_init_dump.c
extern unsigned char db_init_sql[];

int main(int argc, char *argv[]) {
	if (argc < 2) {
		eprintf("Usage: setup-install INSTALL_DIR\n");
		return 1;
	}

	// Get database filename
	size_t len = strlen(argv[1]);
	char db_file[len + strlen(DB_FILENAME) + 2];
	strcpy(db_file, argv[1]);
	if (len && db_file[len - 1] != '/')
		db_file[len++] = '/';
	strcpy(db_file + len, DB_FILENAME);

	// Configure SQLite
	sqlite3_config(SQLITE_CONFIG_SINGLETHREAD);
	sqlite3_config(SQLITE_CONFIG_MEMSTATUS, false);
	sqlite3_initialize();

	// Open database connection
	sqlite3 *db;
	if (sqlite3_open(db_file, &db)) {
		fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		return 1;
	}

	int rc;
	char *err_msg;
	sqlite3_busy_timeout(db, 0);
	if (sqlite3_exec(db, "BEGIN", NULL, NULL, &err_msg)) {
		eprintf("SQLite error: %s\n", err_msg);
		sqlite3_free(err_msg);
		rc = 1;
	}

	if (sqlite3_exec(db, (const char*)db_init_sql, NULL, NULL, &err_msg)) {
		eprintf("SQLite error: %s\n", err_msg);
		sqlite3_free(err_msg);
		rc = 1;
	}

	if (sqlite3_exec(db, "COMMIT", NULL, NULL, &err_msg)) {
		eprintf("SQLite error: %s\n", err_msg);
		sqlite3_free(err_msg);
		rc = 1;
	}

	sqlite3_close(db);
	return rc;
}
