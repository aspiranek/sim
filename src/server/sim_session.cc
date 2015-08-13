#include "sim_session.h"

#include "../include/debug.h"
#include "../include/random.h"
#include "../include/time.h"

#include <cppconn/prepared_statement.h>
#include <cstring>

using std::string;

Sim::Session::State Sim::Session::open() {
	if (state_ != CLOSED)
		return state_;

	state_ = FAIL;

	id_ = sim_.req_->getCookie("session");
	// Cookie does not exist (or have no value)
	if (id_.empty())
		return FAIL;

	sqlite3_stmt *stmt;
	if (sqlite3_prepare_v2(sim_.db, "SELECT user_id, data, type, username, ip, "
			"user_agent FROM session s, users u "
			"WHERE s.id=? AND time>=? AND u.id=s.user_id", -1, &stmt, NULL))
		return FAIL;

	sqlite3_bind_text(stmt, 1, id_.c_str(), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, date("%Y-%m-%d %H:%M:%S",
		time(NULL) - SESSION_MAX_LIFETIME).c_str(), -1, SQLITE_TRANSIENT);

	if (sqlite3_step(stmt) == SQLITE_ROW) {
		user_id = (const char*)sqlite3_column_text(stmt, 0);
		data = (const char*)sqlite3_column_text(stmt, 1);
		user_type = sqlite3_column_int(stmt, 2);
		username = (const char*)sqlite3_column_text(stmt, 3);

		// If no session injection
		if (sim_.client_ip_ == (const char*)sqlite3_column_text(stmt, 4) &&
				sim_.req_->headers.isEqualTo("User-Agent",
					(const char*)sqlite3_column_text(stmt, 5))) {
			sqlite3_finalize(stmt);
			return (state_ = OK);
		}
	}

	sqlite3_finalize(stmt);
	sim_.resp_.setCookie("session", "", 0); // Delete cookie
	return FAIL;
}

static string generate_id() {
	const char t[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
	const size_t len = strlen(t), SESSION_ID_LENGTH = 10;

	string res(SESSION_ID_LENGTH, '0');

	for (size_t i = 0; i < SESSION_ID_LENGTH; ++i)
		res[i] = t[getRandom(0, len - 1)];

	return res;
}

Sim::Session::State Sim::Session::create(const string& _user_id) {
	close();

	// Remove obsolete sessions
	sqlite3_exec(sim_.db, string("BEGIN; DELETE FROM `session` WHERE time<'").
		append(date("%Y-%m-%d %H:%M:%S'",
			time(NULL) - SESSION_MAX_LIFETIME)).c_str(), NULL, NULL, NULL);

	sqlite3_stmt *stmt;
	if (sqlite3_prepare_v2(sim_.db, "INSERT INTO session(id, user_id, ip, data,"
		"user_agent, time) VALUES(?,?,?,'',?,?)", -1, &stmt, NULL)) {
		sqlite3_exec(sim_.db, "ROLLBACK", NULL, NULL, NULL);
		return state_ = FAIL;
	}

	sqlite3_bind_text(stmt, 2, _user_id.c_str(), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 3, sim_.client_ip_.c_str(), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 4, sim_.req_->headers.get("User-Agent").c_str(), -1,
		SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 5, date("%Y-%m-%d %H:%M:%S").c_str(), -1,
		SQLITE_TRANSIENT);

	for (;;) {
		id_ = generate_id();
		sqlite3_bind_text(stmt, 1, id_.c_str(), -1, SQLITE_STATIC);

		if (sqlite3_step(stmt) == SQLITE_DONE)
			break;

		DEBUG_ERROR();

		sqlite3_reset(stmt);
	}

	sqlite3_finalize(stmt);
	sqlite3_exec(sim_.db, "COMMIT", NULL, NULL, NULL);

	sim_.resp_.setCookie("session", id_, time(NULL) + SESSION_MAX_LIFETIME, "/",
		"", true);
	return state_ = OK;
}

void Sim::Session::destroy() {
	if (state_ != OK)
		return;

	sqlite3_stmt *stmt;
	if (sqlite3_prepare_v2(sim_.db, "DELETE FROM session WHERE id=?", -1, &stmt,
			NULL) == SQLITE_OK) {
		sqlite3_bind_text(stmt, 1, id_.c_str(), -1, SQLITE_STATIC);
		sqlite3_step(stmt);
		sqlite3_finalize(stmt);
	}

	sim_.resp_.setCookie("session", "", 0); // Delete cookie
	state_ = CLOSED;
}

void Sim::Session::close() {
	state_ = CLOSED;
	if (state_ != OK)
		return;

	sqlite3_stmt *stmt;
	if (sqlite3_prepare_v2(sim_.db, "UPDATE session SET data=?, time=? "
			"WHERE id=?", -1, &stmt, NULL))
		return;

	sqlite3_bind_text(stmt, 1, data.c_str(), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, date("%Y-%m-%d %H:%M:%S").c_str(), -1,
		SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 3, id_.c_str(), -1, SQLITE_STATIC);
	sqlite3_step(stmt);

	sqlite3_finalize(stmt);
}
