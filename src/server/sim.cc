#include "sim_contest.h"
#include "sim_session.h"
#include "sim_template.h"
#include "sim_user.h"

#include "../include/debug.h"
#include "../include/filesystem.h"
#include "../include/time.h"

#include <stdexcept>

using std::string;

Sim::Sim() : db(NULL), client_ip_(), req_(NULL),
		resp_(server::HttpResponse::TEXT), contest(NULL), session(NULL),
		user(NULL) {
	// Because of exception safety (we do not want to make memory leak)
	try {
		contest = new Contest(*this);
		session = new Session(*this);
		user = new User(*this);

		// Configure SQLite
		sqlite3_config(SQLITE_CONFIG_MULTITHREAD);
		sqlite3_config(SQLITE_CONFIG_MEMSTATUS, false);
		sqlite3_initialize();
		// Open database connection
		if (sqlite3_open(DB_FILENAME, &db)) {
			string msg = string("Cannot open database: ") + sqlite3_errmsg(db);
			sqlite3_close(db);
			throw std::runtime_error(msg);
		}
		sqlite3_busy_timeout(db, 0);

	} catch (...) {
		// Clean up
		delete contest;
		delete session;
		delete user;
		throw;
	}
}

Sim::~Sim() {
	delete contest;
	delete session;
	delete user;
	sqlite3_close(db);
}

server::HttpResponse Sim::handle(string client_ip,
		const server::HttpRequest& req) {
	client_ip_.swap(client_ip);
	req_ = &req;
	resp_ = server::HttpResponse(server::HttpResponse::TEXT);

	E("%s\n", req.target.c_str());

	try {
		if (0 == compareTo(req.target, 1, '/', "kit"))
			getStaticFile();

		else if (0 == compareTo(req.target, 1, '/', "login"))
			user->login();

		else if (0 == compareTo(req.target, 1, '/', "logout"))
			user->logout();

		else if (0 == compareTo(req.target, 1, '/', "signup"))
			user->signUp();

		else if (0 == compareTo(req.target, 1, '/', "u"))
			user->handle();

		else if (0 == compareTo(req.target, 1, '/', "c"))
			contest->handle();

		else if (0 == compareTo(req.target, 1, '/', "s"))
			contest->submission();

		else if (0 == compareTo(req.target, 1, '/', ""))
			mainPage();

		else
			error404();

	} catch (const std::exception& e) {
		E("\e[31mCaught exception: %s:%d\e[m - %s\n", __FILE__, __LINE__,
			e.what());
		error500();

	} catch (...) {
		E("\e[31mCaught exception: %s:%d\e[m\n", __FILE__, __LINE__);
		error500();
	}

	// Make sure that session is closed
	session->close();
	return resp_;
}

void Sim::mainPage() {
	Template templ(*this, "Main page");
	templ << "<div style=\"text-align: center\">\n"
				"<img src=\"/kit/img/SIM-logo.png\" width=\"260\" height=\"336\" alt=\"\">\n"
				"<p style=\"font-size: 30px\">Welcome to SIM</p>\n"
				"<hr>\n"
				"<p>SIM is open source platform for carrying out algorithmic contests</p>\n"
			"</div>\n";
}

void Sim::getStaticFile() {
	string file = "public";
	// Extract path (ignore query)
	file += abspath(decodeURI(req_->target, 1, req_->target.find('?')));
	E("%s\n", file.c_str());

	// Get file stat
	struct stat attr;
	if (stat(file.c_str(), &attr) != -1) {
		// Extract time of last modification
		resp_.headers["Last-Modified"] = date("%a, %d %b %Y %H:%M:%S GMT", attr.st_mtime);
		server::HttpHeaders::const_iterator it = req_->headers.find("if-modified-since");
		struct tm client_mtime;

		// If "If-Modified-Since" header is set and its value is not lower than attr.st_mtime
		if (it != req_->headers.end() && NULL != strptime(it->second.c_str(),
				"%a, %d %b %Y %H:%M:%S GMT", &client_mtime) &&
				timegm(&client_mtime) >= attr.st_mtime) {
			resp_.status_code = "304 Not Modified";
			resp_.content_type = server::HttpResponse::TEXT;
			return;
		}
	}

	resp_.content = file;
	resp_.content_type = server::HttpResponse::FILE;
}

void Sim::redirect(const string& location) {
	resp_.status_code = "302 Moved Temporarily";
	resp_.headers["Location"] = location;
}

int Sim::getUserType(const char* user_id) {
	int rc = 3;
	sqlite3_stmt *stmt;
	if (sqlite3_prepare_v2(db, "SELECT type FROM users WHERE id=?", -1, &stmt,
		NULL))
		return rc;

	sqlite3_bind_text(stmt, 1, user_id, -1, SQLITE_STATIC);

	if (sqlite3_step(stmt) == SQLITE_ROW)
		rc = sqlite3_column_int(stmt, 0);

	sqlite3_finalize(stmt);
	return rc;
}
