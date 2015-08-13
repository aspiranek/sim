#include "judge.h"
#include "main.h"

#include "../include/db.h"
#include "../include/debug.h"
#include "../include/string.h"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <limits.h>
#include <sys/inotify.h>

using std::string;

static const int OLD_WATCH_METHOD_SLEEP = 1 * 1000000; // 1s
static sqlite3 *db;
UniquePtr<TemporaryDirectory> tmp_dir;
unsigned VERBOSITY = 2; // 0 - quiet, 1 - normal, 2 or more - verbose

static void processSubmissionQueue() {
	sqlite3_stmt *stmt, *pstmt = NULL;
	sqlite3_prepare_v2(db, "SELECT id, user_id, round_id, problem_id "
		"FROM submissions "
		"WHERE status='waiting' ORDER BY queued LIMIT 10", -1, &stmt, NULL);
	// While submission queue is not empty
	for (;;) {
		sqlite3_reset(stmt);

		if (sqlite3_step(stmt) != SQLITE_ROW)
			goto exit;

		do {
			const char* submission_id = (const char*)sqlite3_column_text(stmt,
				0);
			const char* user_id = (const char*)sqlite3_column_text(stmt, 1);
			const char* round_id = (const char*)sqlite3_column_text(stmt, 2);
			const char* problem_id = (const char*)sqlite3_column_text(stmt, 3);

			// Judge
			JudgeResult jres = judge(submission_id, problem_id);

			// Update submission
			putFileContents(string("submissions/") + submission_id,
				jres.content);

			sqlite3_exec(db, "BEGIN", NULL, NULL, NULL);
			// Update final
			if (jres.status != JudgeResult::COMPILE_ERROR) {
				// Remove old final:
				// From submissions_to_rounds
				sqlite3_prepare_v2(db, "UPDATE submissions_to_rounds "
					"SET final=false WHERE submission_id="
						"(SELECT id FROM submissions WHERE round_id=? AND "
						"user_id=? AND final=true LIMIT 1)", -1, &pstmt, NULL);
				sqlite3_bind_text(pstmt, 1, round_id, -1, SQLITE_STATIC);
				sqlite3_bind_text(pstmt, 2, user_id, -1, SQLITE_STATIC);

				if (sqlite3_step(pstmt) != SQLITE_DONE) {
					eprintf("Error: sqlite3_step() - %s:%i\n", __FILE__,
						__LINE__);
					sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
					goto exit;
				}

				// From submissions
				sqlite3_finalize(pstmt);
				sqlite3_prepare_v2(db, "UPDATE submissions SET final=false "
					"WHERE round_id=? AND user_id=? AND final=true", -1, &pstmt,
					NULL);
				sqlite3_bind_text(pstmt, 1, round_id, -1, SQLITE_STATIC);
				sqlite3_bind_text(pstmt, 2, user_id, -1, SQLITE_STATIC);

				if (sqlite3_step(pstmt) != SQLITE_DONE) {
					eprintf("Error: sqlite3_step() - %s:%i\n", __FILE__,
						__LINE__);
					sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
					goto exit;
				}

				// Set new final in submissions_to_rounds
				sqlite3_finalize(pstmt);
				sqlite3_prepare_v2(db, "UPDATE submissions_to_rounds "
					"SET final=true WHERE submission_id=?", -1, &pstmt, NULL);
				sqlite3_bind_text(pstmt, 1, submission_id, -1, SQLITE_STATIC);

				if (sqlite3_step(pstmt) != SQLITE_DONE) {
					eprintf("Error: sqlite3_step() - %s:%i\n", __FILE__,
						__LINE__);
					sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
					goto exit;
				}
				sqlite3_finalize(pstmt);
			}

			// Update submission
			sqlite3_prepare_v2(db, "UPDATE submissions SET status=?, score=?, "
				"final=? WHERE id=?", -1, &pstmt, NULL);

			switch (jres.status) {
			case JudgeResult::OK:
				sqlite3_bind_int(pstmt, 1, OK);
				break;

			case JudgeResult::ERROR:
				sqlite3_bind_int(pstmt, 1, ERROR);
				break;

			case JudgeResult::COMPILE_ERROR:
				sqlite3_bind_int(pstmt, 1, COMPILE_ERROR);
				sqlite3_bind_null(pstmt, 2);
				sqlite3_bind_int(pstmt, 3, false);
				break;

			case JudgeResult::JUDGE_ERROR:
				sqlite3_bind_int(pstmt, 1, JUDGE_ERROR);
				sqlite3_bind_null(pstmt, 2);
				sqlite3_bind_int(pstmt, 3, false);
			}

			if (jres.status != JudgeResult::COMPILE_ERROR &&
					jres.status != JudgeResult::JUDGE_ERROR) {
				sqlite3_bind_int(pstmt, 2, jres.score);
				sqlite3_bind_int(pstmt, 3, true);
			}

			sqlite3_bind_text(pstmt, 4, submission_id, -1, SQLITE_STATIC);
			if (sqlite3_step(pstmt) != SQLITE_DONE) {
				eprintf("Error: sqlite3_step() - %s:%i\n", __FILE__,
					__LINE__);
				sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
				goto exit;
			}

			sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
		} while (sqlite3_step(stmt) == SQLITE_ROW);
	}
exit:
	sqlite3_finalize(stmt);
	sqlite3_finalize(pstmt);
}

void startWatching(int inotify_fd, int& wd) {
	while ((wd = inotify_add_watch(inotify_fd, "judge-machine.notify",
			IN_ATTRIB)) == -1) {
		eprintf("Error: inotify_add_watch() - %s\n", strerror(errno));
		// Run tests
		processSubmissionQueue();
		usleep(OLD_WATCH_METHOD_SLEEP); // sleep

		if (access("judge-machine.notify", F_OK) == -1)
			createFile("judge-machine.notify", S_IRUSR);
	}
}

int main() {
	// Install signal handlers
	struct sigaction sa;
	memset (&sa, 0, sizeof(sa));
	sa.sa_handler = &exit;

	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGQUIT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	// Configure SQLite
	sqlite3_config(SQLITE_CONFIG_SINGLETHREAD);
	sqlite3_config(SQLITE_CONFIG_MEMSTATUS, false);
	sqlite3_initialize();

	// Open database connection
	if (sqlite3_open(DB_FILENAME, &db)) {
		eprintf("Cannot open database: %s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		return 1;
	}

	sqlite3_busy_timeout(db, 0);

	// Create tmp_dir
	tmp_dir.reset(new TemporaryDirectory("/tmp/sim-judge-machine.XXXXXX"));

	// Initialise inotify
	int inotify_fd, wd;
	while ((inotify_fd = inotify_init()) == -1) {
		eprintf("Error: inotify_init() - %s\n", strerror(errno));
		// Run tests
		processSubmissionQueue();
		usleep(OLD_WATCH_METHOD_SLEEP); // sleep
	}

	// If "judge-machine.notify" file does not exist create it
	if (access("judge-machine.notify", F_OK) == -1)
		createFile("judge-machine.notify", S_IRUSR);

	startWatching(inotify_fd, wd);

	// Inotify buffer
	ssize_t len;
	char inotify_buff[sizeof(inotify_event) + NAME_MAX + 1];

	// Run tests before waiting for notification
	processSubmissionQueue();

	// Wait for notification
	for (;;) {
		len = read(inotify_fd, inotify_buff, sizeof(inotify_buff));
		if (len < 1) {
			eprintf("Error: read() - %s\n", strerror(errno));
			continue;
		}

		// If notify file disappeared
		if (access("judge-machine.notify", F_OK) == -1) {
			createFile("judge-machine.notify", S_IRUSR);

			inotify_rm_watch(inotify_fd, wd);
			startWatching(inotify_fd, wd);
		}

		// Run tests
		processSubmissionQueue();
	}

	sqlite3_close(db);
	return 0;
}
