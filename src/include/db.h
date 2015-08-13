#pragma once

#include <sqlite3.h>

#define DB_FILENAME ".sim.db"

enum SubmissionStatus {
	OK = 0,
	ERROR = 1,
	COMPILE_ERROR = 2,
	JUDGE_ERROR = 3,
	WAITING = 4
};

/*namespace DB {

class Transaction {
};

class

} // namespace DB*/
