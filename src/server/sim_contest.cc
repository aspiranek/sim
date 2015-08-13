#include "form_validator.h"
#include "sim_contest_utility.h"
#include "sim_session.h"

#include "../include/debug.h"
#include "../include/filesystem.h"
#include "../include/process.h"
#include "../include/time.h"

#include <cerrno>
#include <utime.h>

#define foreach(i,x) for (__typeof(x.begin()) i = x.begin(), \
	i ##__end = x.end(); i != i ##__end; ++i)

using std::string;
using std::vector;

Sim::Contest::~Contest() { delete r_path_; }

void Sim::Contest::handle() {
	size_t arg_beg = 3;

	// Select contest
	if (0 == compareTo(sim_.req_->target, arg_beg, '/', "")) {
		Template templ(sim_, "Select contest");
		sqlite3_stmt *stmt;
		// Get available contests
		if (sim_.session->open() == Session::OK) {
			if (sqlite3_prepare_v2(sim_.db,
					"(SELECT r.id, r.name FROM rounds r, users u "
						"WHERE parent IS NULL AND r.owner=u.id AND "
							"(r.access='public' OR r.owner=? OR u.type>?))"
					" UNION "
					"(SELECT id, name FROM rounds, users_to_contests "
						"WHERE user_id=? AND contest_id=id) ORDER BY id", -1,
					&stmt, NULL))
				return sim_.error500();


			sqlite3_bind_text(stmt, 1, sim_.session->user_id.c_str(), -1,
				SQLITE_STATIC);
			sqlite3_bind_int(stmt, 2, sim_.session->user_type);
			sqlite3_bind_text(stmt, 3, sim_.session->user_id.c_str(), -1,
				SQLITE_STATIC);

		} else if (sqlite3_prepare_v2(sim_.db,"SELECT id, name FROM rounds "
				"WHERE parent IS NULL AND public_access ORDER BY id", -1,
				&stmt, NULL))
			return sim_.error500();

		// List them
		templ << "<div class=\"contests-list\">\n";
		// Add contest button (admins and teachers only)
		if (sim_.session->state() == Session::OK &&
				sim_.session->user_type < 2)
			templ << "<a class=\"btn\" href=\"/c/add\">Add contest</a>\n";

		while (sqlite3_step(stmt) == SQLITE_ROW)
			templ << "<a href=\"/c/"
				<< htmlSpecialChars((const char*)sqlite3_column_text(stmt, 0))
				<< "\">"
				<< htmlSpecialChars((const char*)sqlite3_column_text(stmt, 1))
				<< "</a>\n";

		templ << "</div>\n";
		sqlite3_finalize(stmt);

	// Add contest
	} else if (0 == compareTo(sim_.req_->target, arg_beg, '/', "add"))
		addContest();

	// Other pages which need round id
	else {
		// Extract round id
		string round_id;
		int res_code = strToNum(round_id, sim_.req_->target, arg_beg, '/');
		if (res_code == -1)
			return sim_.error404();

		arg_beg += res_code + 1;

		// Get parent rounds
		delete r_path_;
		r_path_ = getRoundPath(round_id);
		if (r_path_ == NULL)
			return;

		// Check if user forces observer view
		bool admin_view = r_path_->admin_access;
		if (0 == compareTo(sim_.req_->target, arg_beg, '/', "n")) {
			admin_view = false;
			arg_beg += 2;
		}

		// Problem statement
		if (r_path_->type == PROBLEM &&
				0 == compareTo(sim_.req_->target, arg_beg, '/', "statement")) {
			string statement = getFileByLines("problems/" +
				r_path_->problem->problem_id + "/conf.cfg", GFBL_IGNORE_NEW_LINES,
				2, 3)[0];

			if (isSuffix(statement, ".pdf"))
				sim_.resp_.headers["Content-type"] = "application/pdf";
			else if (isSuffix(statement, ".html") ||
					isSuffix(statement, ".htm"))
				sim_.resp_.headers["Content-type"] = "text/html";
			else if (isSuffix(statement, ".txt") || isSuffix(statement, ".md"))
				sim_.resp_.headers["Content-type"] = "text/plain; charset=utf-8";

			sim_.resp_.content_type = server::HttpResponse::FILE;
			sim_.resp_.content.clear();
			sim_.resp_.content.append("problems/").append(r_path_->problem->problem_id).
				append("/doc/").append(statement);
			return;
		}

		// Add
		if (0 == compareTo(sim_.req_->target, arg_beg, '/', "add")) {
			if (r_path_->type == CONTEST)
				return addRound();

			if (r_path_->type == ROUND)
				return addProblem();

			return sim_.error404();
		}

		// Edit
		if (0 == compareTo(sim_.req_->target, arg_beg, '/', "edit")) {
			if (r_path_->type == CONTEST)
				return editContest();

			if (r_path_->type == ROUND)
				return editRound();

			return editProblem();
		}

		// Delete
		if (0 == compareTo(sim_.req_->target, arg_beg, '/', "delete")) {
			if (r_path_->type == CONTEST)
				return deleteContest();

			if (r_path_->type == ROUND)
				return deleteRound();

			return deleteProblem();
		}

		// Problems
		if (0 == compareTo(sim_.req_->target, arg_beg, '/', "problems"))
			return problems(admin_view);

		// Submit
		if (0 == compareTo(sim_.req_->target, arg_beg, '/', "submit"))
			return submit(admin_view);

		// Submissions
		if (0 == compareTo(sim_.req_->target, arg_beg, '/', "submissions"))
			return submissions(admin_view);

		// Contest dashboard
		TemplateWithMenu templ(*this, "Contest dashboard");

		templ << "<h1>Dashboard</h1>";
		templ.printRoundPath(*r_path_, "");
		templ.printRoundView(*r_path_, false, admin_view);

		if (r_path_->type == PROBLEM)
			templ << "<a class=\"btn\" href=\"/c/" << r_path_->round_id
				<< "/statement\" style=\"margin:5px auto 5px auto\">"
				"View statement</a>\n";
	}
}

void Sim::Contest::addContest() {
	if (sim_.session->open() != Sim::Session::OK || sim_.session->user_type > 1)
		return sim_.error403();

	FormValidator fv(sim_.req_->form_data);
	string name;
	bool is_public = false;

	if (sim_.req_->method == server::HttpRequest::POST) {
		// Validate all fields
		fv.validateNotBlank(name, "name", "Contest name", 128);
		is_public = fv.exist("public");

		// If all fields are ok
		if (fv.noErrors()) {
			sqlite3_stmt *stmt;
			if (sqlite3_prepare_v2(sim_.db,
					"INSERT INTO rounds (access, name, owner, item) "
					"SELECT ?, ?, ?, MAX(item)+1 FROM rounds "
					"WHERE parent IS NULL", -1, &stmt, NULL))
				return sim_.error500();

				sqlite3_bind_text(stmt, 1, is_public ? "public" : "private", -1,
					SQLITE_STATIC);
				sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_STATIC);
				sqlite3_bind_text(stmt, 3, sim_.session->user_id.c_str(), -1,
					SQLITE_STATIC);

				if (sqlite3_step(stmt) == SQLITE_DONE) {
					sqlite3_finalize(stmt);
					if (sqlite3_prepare_v2(sim_.db,
							"SELECT LAST_INSERT_ROWID()", -1, &stmt, NULL))
						return sim_.error500();

					if (sqlite3_step(stmt) == SQLITE_ROW) {
						sqlite3_finalize(stmt);
						return sim_.redirect(string("/c/") +
							(const char*)sqlite3_column_text(stmt, 0));
					}
				}
			sqlite3_finalize(stmt);
		}
	}

	Sim::Template templ(sim_, "Add contest", ".body{margin-left:30px}");
	templ << fv.errors() << "<div class=\"form-container\">\n"
			"<h1>Add contest</h1>\n"
			"<form method=\"post\">\n"
				// Name
				"<div class=\"field-group\">\n"
					"<label>Contest name</label>\n"
					"<input type=\"text\" name=\"name\" value=\""
						<< htmlSpecialChars(name) << "\" size=\"24\" maxlength=\"128\" required>\n"
				"</div>\n"
				// Public
				"<div class=\"field-group\">\n"
					"<label>Public</label>\n"
					"<input type=\"checkbox\" name=\"public\""
						<< (is_public ? " checked" : "") << ">\n"
				"</div>\n"
				"<input class=\"btn\" type=\"submit\" value=\"Add\">\n"
			"</form>\n"
		"</div>\n";
}

void Sim::Contest::addRound() {
	if (!r_path_->admin_access)
		return sim_.error403();

	FormValidator fv(sim_.req_->form_data);
	string name;
	bool is_visible = false;
	string begins, full_results, ends;

	if (sim_.req_->method == server::HttpRequest::POST) {
		// Validate all fields
		fv.validateNotBlank(name, "name", "Round name", 128);
		is_visible = fv.exist("visible");
		fv.validate(begins, "begins", "Begins", isDatetimeOrBlank, "Begins: invalid value");
		fv.validate(ends, "ends", "Ends", isDatetimeOrBlank, "Ends: invalid value");
		fv.validate(full_results, "full_results", "Ends", isDatetimeOrBlank, "Full_results: invalid value");

		// If all fields are ok
		if (fv.noErrors()) {
			sqlite3_stmt *stmt;
			if (sqlite3_prepare_v2(sim_.db,
					"INSERT INTO rounds (parent, name, item, visible, begins, "
						"ends, full_results) "
					"SELECT ?, ?, MAX(item)+1, ?, ?, ?, ? FROM rounds "
					"WHERE parent=?", -1, &stmt, NULL))
				return sim_.error500();

			sqlite3_bind_text(stmt, 1, r_path_->round_id.c_str(), -1,
				SQLITE_STATIC);
			sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_int(stmt, 3, is_visible);

			// Begins
			if (begins.empty())
				sqlite3_bind_null(stmt, 4);
			else
				sqlite3_bind_text(stmt, 4, begins.c_str(), -1, SQLITE_STATIC);

			// ends
			if (ends.empty())
				sqlite3_bind_null(stmt, 5);
			else
				sqlite3_bind_text(stmt, 5, ends.c_str(), -1, SQLITE_STATIC);

			// Full_results
			if (full_results.empty())
				sqlite3_bind_null(stmt, 6);
			else
				sqlite3_bind_text(stmt, 6, full_results.c_str(), -1,
					SQLITE_STATIC);

			sqlite3_bind_text(stmt, 7, r_path_->round_id.c_str(), -1,
				SQLITE_STATIC);

			if (sqlite3_step(stmt) == SQLITE_DONE) {
				sqlite3_finalize(stmt);
				if (sqlite3_prepare_v2(sim_.db, "SELECT LAST_INSERT_ROWID()",
						-1, &stmt, NULL))
					return sim_.error500();

				if (sqlite3_step(stmt) == SQLITE_ROW) {
					sqlite3_finalize(stmt);
					return sim_.redirect(string("/c/") +
						(const char*)sqlite3_column_text(stmt, 0));
				}
			}
			sqlite3_finalize(stmt);
		}
	}

	TemplateWithMenu templ(*this, "Add round");
	templ.printRoundPath(*r_path_, "");
	templ << fv.errors() << "<div class=\"form-container\">\n"
		"<h1>Add round</h1>\n"
		"<form method=\"post\">\n"
			// Name
			"<div class=\"field-group\">\n"
				"<label>Round name</label>\n"
				"<input type=\"text\" name=\"name\" value=\""
					<< htmlSpecialChars(name) << "\" size=\"24\" maxlength=\"128\" required>\n"
			"</div>\n"
			// Visible
			"<div class=\"field-group\">\n"
				"<label>Visible</label>\n"
				"<input type=\"checkbox\" name=\"visible\""
					<< (is_visible ? " checked" : "") << ">\n"
			"</div>\n"
			// Begins
			"<div class=\"field-group\">\n"
				"<label>Begins</label>\n"
				"<input type=\"text\" name=\"begins\""
					"placeholder=\"yyyy-mm-dd HH:MM:SS\" value=\""
					<< htmlSpecialChars(begins) << "\" size=\"19\" maxlength=\"19\">\n"
			"</div>\n"
			// Ends
			"<div class=\"field-group\">\n"
				"<label>Ends</label>\n"
				"<input type=\"text\" name=\"ends\""
					"placeholder=\"yyyy-mm-dd HH:MM:SS\" value=\""
					<< htmlSpecialChars(ends) << "\" size=\"19\" maxlength=\"19\">\n"
			"</div>\n"
			// Full_results
			"<div class=\"field-group\">\n"
				"<label>Full_results</label>\n"
				"<input type=\"text\" name=\"full_results\""
					"placeholder=\"yyyy-mm-dd HH:MM:SS\" value=\""
					<< htmlSpecialChars(full_results) << "\" size=\"19\" maxlength=\"19\">\n"
			"</div>\n"
			"<input class=\"btn\" type=\"submit\" value=\"Add\">\n"
		"</form>\n"
	"</div>\n";
}

void Sim::Contest::addProblem() {
	if (!r_path_->admin_access)
		return sim_.error403();

	FormValidator fv(sim_.req_->form_data);
	string name, user_package_file;
	bool force_auto_limit = false;

	if (sim_.req_->method == server::HttpRequest::POST) {
		// Validate all fields
		fv.validate(name, "name", "Problem name", 128);

		force_auto_limit = fv.exist("force-auto-limit");

		fv.validateNotBlank(user_package_file, "package", "Package");

		// If all fields are OK
		if (fv.noErrors()) {
			string package_file = fv.getFilePath("package");

			// Rename package file that it will end with original extension
			string new_package_file = package_file + '.' +
				(isSuffix(user_package_file, ".tar.gz") ? "tar.gz"
					: getExtension(user_package_file));
			if (link(package_file.c_str(), new_package_file.c_str()))
				return sim_.error500();

			// Create temporary directory for holding package
			char package_tmp_dir[] = "/tmp/sim-problem.XXXXXX";
			if (mkdtemp(package_tmp_dir))
				return sim_.error500();

			DirectoryRemover rm_tmp_dir(package_tmp_dir);

			// Construct Conver arguments
			vector<string> args(1, "./conver");
			append(args)(new_package_file)("-o")(package_tmp_dir)("-uc")("-q");

			if (force_auto_limit)
				args.push_back("-fal");

			if (name.size())
				append(args)("-n")(name);

			// Conver stdin, stdout, stderr
			spawn_opts sopt = {
				-1,
				-1,
				getUnlinkedTmpFile()
			};

			if (sopt.new_stderr_fd == -1)
				return sim_.error500();

			// Convert package
			if (0 != spawn("./conver", args, &sopt)) {
				fv.addError("Conver failed - " +
					getFileContents(sopt.new_stderr_fd));
				goto form;
			}

			// Insert problem
			sqlite3_stmt *stmt;
			if (sqlite3_prepare_v2(sim_.db, "INSERT INTO problems"
					"(name, owner, added) VALUES(?, ?, ?)", -1, &stmt, NULL))
				return sim_.error500();

			name.swap(getFileByLines(string(package_tmp_dir) + "/conf.cfg",
				GFBL_IGNORE_NEW_LINES, 0, 1)[0]);
			sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt, 2, sim_.session->user_id.c_str(), -1,
				SQLITE_STATIC);
			sqlite3_bind_text(stmt, 3, date("%Y-%m-%d %H:%M:%S").c_str(), -1,
				SQLITE_TRANSIENT);

			sqlite3_exec(sim_.db, "BEGIN", NULL, NULL, NULL);
			if (sqlite3_step(stmt) != SQLITE_DONE) {
				sqlite3_finalize(stmt);
				sqlite3_exec(sim_.db, "ROLLBACK", NULL, NULL, NULL);
				return sim_.error500();
			}

			// Get problem_id
			sqlite3_finalize(stmt);
			if (sqlite3_prepare_v2(sim_.db, "SELECT LAST_INSERT_ROWID()", -1,
					&stmt, NULL)) {
				sqlite3_exec(sim_.db, "ROLLBACK", NULL, NULL, NULL);
				return sim_.error500();
			}

			if (sqlite3_step(stmt) != SQLITE_ROW) {
				sqlite3_finalize(stmt);
				sqlite3_exec(sim_.db, "ROLLBACK", NULL, NULL, NULL);
				return sim_.error500();
			}

			string problem_id = (const char*)sqlite3_column_text(stmt, 0);

			// Insert round
			sqlite3_finalize(stmt);
			if (sqlite3_prepare_v2(sim_.db, "INSERT INTO rounds"
					"(parent, grandparent, name, item, problem_id) "
					"SELECT ?, ?, ?, MAX(item)+1, ? FROM rounds WHERE parent=?",
					-1, &stmt, NULL)) {
				sqlite3_exec(sim_.db, "ROLLBACK", NULL, NULL, NULL);
				return sim_.error500();
			}


			sqlite3_bind_text(stmt, 1, r_path_->round->id.c_str(), -1,
				SQLITE_STATIC);
			sqlite3_bind_text(stmt, 2, r_path_->contest->id.c_str(), -1,
				SQLITE_STATIC);
			sqlite3_bind_text(stmt, 3, name.c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt, 4, problem_id.c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt, 5, r_path_->round->id.c_str(), -1,
				SQLITE_STATIC);

			if (sqlite3_step(stmt) != SQLITE_DONE) {
				sqlite3_exec(sim_.db, "ROLLBACK", NULL, NULL, NULL);
				return sim_.error500();
			}

			sqlite3_exec(sim_.db, "COMMIT", NULL, NULL, NULL);
			// Move package folder to problems/
			rename(package_tmp_dir, ("problems/" + problem_id).c_str());
			// Cancel folder deletion
			rm_tmp_dir.cancel();

			sqlite3_finalize(stmt);
			if (sqlite3_prepare_v2(sim_.db, "SELECT LAST_INSERT_ROWID()", -1,
					&stmt, NULL))
				return sim_.error500();

			if (sqlite3_step(stmt) != SQLITE_ROW) {
				sqlite3_finalize(stmt);
				return sim_.redirect("/c");
			}

			string location = "/c/";
			location += (const char*)sqlite3_column_text(stmt, 0);
			sqlite3_finalize(stmt);
			return sim_.redirect(location);
		}
	}

 form:
	TemplateWithMenu templ(*this, "Add problem");
	templ.printRoundPath(*r_path_, "");
	templ << fv.errors() << "<div class=\"form-container\">\n"
			"<h1>Add problem</h1>\n"
			"<form method=\"post\" enctype=\"multipart/form-data\">\n"
				// Name
				"<div class=\"field-group\">\n"
					"<label>Problem name</label>\n"
					"<input type=\"text\" name=\"name\" value=\""
						<< htmlSpecialChars(name) << "\" size=\"24\""
					"maxlength=\"128\" placeholder=\"Detect from conf.cfg\">\n"
				"</div>\n"
				// Force auto limit
				"<div class=\"field-group\">\n"
					"<label>Automatic time limit setting</label>\n"
					"<input type=\"checkbox\" name=\"force-auto-limit\""
						<< (force_auto_limit ? " checked" : "") << ">\n"
				"</div>\n"
				// Package
				"<div class=\"field-group\">\n"
					"<label>Package</label>\n"
					"<input type=\"file\" name=\"package\" required>\n"
				"</div>\n"
				"<input class=\"btn\" type=\"submit\" value=\"Add\">\n"
			"</form>\n"
		"</div>\n";
}

void Sim::Contest::editContest() {
	if (!r_path_->admin_access)
		return sim_.error403();

	FormValidator fv(sim_.req_->form_data);
	string name, owner;
	bool is_public = false;
	sqlite3_stmt *stmt;

	if (sim_.req_->method == server::HttpRequest::POST) {
		// Validate all fields
		fv.validateNotBlank(name, "name", "Contest name", 128);
		fv.validateNotBlank(owner, "owner", "Owner username", 30);
		is_public = fv.exist("public");

		// If all fields are ok
		if (fv.noErrors()) {
			if (sqlite3_prepare_v2(sim_.db, "UPDATE rounds SET name=?, "
					"owner=(SELECT id FROM users WHERE username=?), access=? "
					"WHERE id=?", -1, &stmt, NULL))
				return sim_.error500();
			sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt, 2, owner.c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt, 3, is_public ? "public" : "private", -1,
				SQLITE_STATIC);
			sqlite3_bind_text(stmt, 4, r_path_->round_id.c_str(), -1,
				SQLITE_STATIC);

			if (sqlite3_step(stmt) == SQLITE_DONE) {
				fv.addError("Update successful");
				// Update r_path_
				delete r_path_;
				r_path_ = getRoundPath(r_path_->round_id);
				if (r_path_ == NULL) {
					sqlite3_finalize(stmt);
					return;
				}
			}
			sqlite3_finalize(stmt);
		}
	}

	// Get contest information
	name = r_path_->contest->name;
	if (sqlite3_prepare_v2(sim_.db, "SELECT username, public_access "
			"FROM rounds r, users u WHERE r.id=? AND owner=u.id", -1, &stmt,
			NULL))
		return sim_.error500();
	sqlite3_bind_text(stmt, 1, r_path_->round_id.c_str(), -1, SQLITE_STATIC);

	if (sqlite3_step(stmt) == SQLITE_DONE) {
		owner = (const char*)sqlite3_column_text(stmt, 1);
		is_public = sqlite3_column_int(stmt, 2);
	}
	sqlite3_finalize(stmt);

	TemplateWithMenu templ(*this, "Edit contest");
	templ.printRoundPath(*r_path_, "");
	templ << fv.errors() << "<div class=\"form-container\">\n"
			"<h1>Edit contest</h1>\n"
			"<form method=\"post\">\n"
				// Name
				"<div class=\"field-group\">\n"
					"<label>Contest name</label>\n"
					"<input type=\"text\" name=\"name\" value=\""
						<< htmlSpecialChars(name) << "\" size=\"24\" maxlength=\"128\" required>\n"
				"</div>\n"
				// Owner
				"<div class=\"field-group\">\n"
					"<label>Owner username</label>\n"
					"<input type=\"text\" name=\"owner\" value=\""
						<< htmlSpecialChars(owner) << "\" size=\"24\" maxlength=\"30\" required>\n"
				"</div>\n"
				// Public
				"<div class=\"field-group\">\n"
					"<label>Public</label>\n"
					"<input type=\"checkbox\" name=\"public\""
						<< (is_public ? " checked" : "") << ">\n"
				"</div>\n"
				"<div>\n"
					"<input class=\"btn\" type=\"submit\" value=\"Update\">\n"
					"<a class=\"btn-danger\" style=\"float:right\" href=\"/c/"
						<< r_path_->round_id << "/delete\">Delete contest</a>\n"
				"</div>\n"
			"</form>\n"
		"</div>\n";
}

void Sim::Contest::editRound() {
	if (!r_path_->admin_access)
		return sim_.error403();

	FormValidator fv(sim_.req_->form_data);
	string name;
	bool is_visible = false;
	string begins, full_results, ends;

	if (sim_.req_->method == server::HttpRequest::POST) {
		// Validate all fields
		fv.validateNotBlank(name, "name", "Round name", 128);
		is_visible = fv.exist("visible");
		fv.validate(begins, "begins", "Begins", isDatetimeOrBlank, "Begins: invalid value");
		fv.validate(ends, "ends", "Ends", isDatetimeOrBlank, "Ends: invalid value");
		fv.validate(full_results, "full_results", "Ends", isDatetimeOrBlank, "Full_results: invalid value");

		// If all fields are ok
		if (fv.noErrors()) {
			sqlite3_stmt *stmt;
			if (sqlite3_prepare_v2(sim_.db, "UPDATE rounds "
					"SET name=?, visible=?, begins=?, ends=?, full_results=? "
					"WHERE id=?", -1, &stmt, NULL))
				return sim_.error500();

			sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_int(stmt, 2, is_visible);

			// Begins
			if (begins.size())
				sqlite3_bind_text(stmt, 3, begins.c_str(), -1, SQLITE_STATIC);

			// ends
			if (ends.size())
				sqlite3_bind_text(stmt, 4, ends.c_str(), -1, SQLITE_STATIC);

			// Full_results
			if (full_results.size())
				sqlite3_bind_text(stmt, 5, full_results.c_str(), -1,
					SQLITE_STATIC);

			sqlite3_bind_text(stmt, 6, r_path_->round_id.c_str(), -1,
				SQLITE_STATIC);

			if (sqlite3_step(stmt) == SQLITE_DONE) {
				fv.addError("Update successful");
				// Update r_path_
				delete r_path_;
				r_path_ = getRoundPath(r_path_->round_id);
				if (r_path_ == NULL) {
					sqlite3_finalize(stmt);
					return;
				}
			}
			sqlite3_finalize(stmt);
		}
	}

	// Get round information
	name = r_path_->round->name;
	is_visible = r_path_->round->visible;
	begins = r_path_->round->begins;
	ends = r_path_->round->ends;
	full_results = r_path_->round->full_results;

	TemplateWithMenu templ(*this, "Edit round");
	templ.printRoundPath(*r_path_, "");
	templ << fv.errors() << "<div class=\"form-container\">\n"
		"<h1>Edit round</h1>\n"
		"<form method=\"post\">\n"
			// Name
			"<div class=\"field-group\">\n"
				"<label>Round name</label>\n"
				"<input type=\"text\" name=\"name\" value=\""
					<< htmlSpecialChars(name) << "\" size=\"24\" maxlength=\"128\" required>\n"
			"</div>\n"
			// Visible
			"<div class=\"field-group\">\n"
				"<label>Visible</label>\n"
				"<input type=\"checkbox\" name=\"visible\""
					<< (is_visible ? " checked" : "") << ">\n"
			"</div>\n"
			// Begins
			"<div class=\"field-group\">\n"
				"<label>Begins</label>\n"
				"<input type=\"text\" name=\"begins\""
					"placeholder=\"yyyy-mm-dd HH:MM:SS\" value=\""
					<< htmlSpecialChars(begins) << "\" size=\"19\" maxlength=\"19\">\n"
			"</div>\n"
			// Ends
			"<div class=\"field-group\">\n"
				"<label>Ends</label>\n"
				"<input type=\"text\" name=\"ends\""
					"placeholder=\"yyyy-mm-dd HH:MM:SS\" value=\""
					<< htmlSpecialChars(ends) << "\" size=\"19\" maxlength=\"19\">\n"
			"</div>\n"
			// Full_results
			"<div class=\"field-group\">\n"
				"<label>Full_results</label>\n"
				"<input type=\"text\" name=\"full_results\""
					"placeholder=\"yyyy-mm-dd HH:MM:SS\" value=\""
					<< htmlSpecialChars(full_results) << "\" size=\"19\" maxlength=\"19\">\n"
			"</div>\n"
			"<div>\n"
				"<input class=\"btn\" type=\"submit\" value=\"Update\">\n"
				"<a class=\"btn-danger\" style=\"float:right\" href=\"/c/"
					<< r_path_->round_id << "/delete\">Delete round</a>\n"
			"</div>\n"
		"</form>\n"
	"</div>\n";
}

void Sim::Contest::editProblem() {
	if (!r_path_->admin_access)
		return sim_.error403();

	FormValidator fv(sim_.req_->form_data);

	TemplateWithMenu templ(*this, "Edit problem");
	templ.printRoundPath(*r_path_, "");
}

void Sim::Contest::deleteContest() {
	if (!r_path_->admin_access)
		return sim_.error403();

	FormValidator fv(sim_.req_->form_data);
	if (sim_.req_->method == server::HttpRequest::POST)
		if (fv.exist("delete")) {
			sqlite3_stmt *stmt;
			// Delete submissions
			if (sqlite3_prepare_v2(sim_.db,
					"DELETE FROM submissions, submissions_to_rounds "
					"USING submissions INNER JOIN submissions_to_rounds "
					"WHERE contest_round_id=? AND id=submission_id", -1, &stmt,
					NULL))
				return sim_.error500();

			sqlite3_bind_text(stmt, 1, r_path_->round_id.c_str(), -1,
				SQLITE_STATIC);

			sqlite3_exec(sim_.db, "BEGIN", NULL, NULL, NULL);
			if (sqlite3_step(stmt) != SQLITE_DONE) {
				sqlite3_finalize(stmt);
				sqlite3_exec(sim_.db, "ROLLBACK", NULL, NULL, NULL);
				return sim_.error500();
			}
			sqlite3_finalize(stmt);

			// Delete from users_to_contests
			if (sqlite3_prepare_v2(sim_.db,"DELETE FROM users_to_contests "
					"WHERE contest_id=?", -1, &stmt, NULL)) {
				sqlite3_exec(sim_.db, "ROLLBACK", NULL, NULL, NULL);
				return sim_.error500();
			}

			sqlite3_bind_text(stmt, 1, r_path_->round_id.c_str(), -1,
				SQLITE_STATIC);
			if (sqlite3_step(stmt) != SQLITE_DONE) {
				sqlite3_finalize(stmt);
				sqlite3_exec(sim_.db, "ROLLBACK", NULL, NULL, NULL);
				return sim_.error500();
			}
			sqlite3_finalize(stmt);

			// Delete rounds
			if (sqlite3_prepare_v2(sim_.db,"DELETE FROM rounds WHERE id=?1 "
					"OR parent=?1 OR grandparent=?1", -1, &stmt, NULL)) {
				sqlite3_exec(sim_.db, "ROLLBACK", NULL, NULL, NULL);
				return sim_.error500();
			}

			sqlite3_bind_text(stmt, 1, r_path_->round_id.c_str(), -1,
				SQLITE_STATIC);

			int rc = sqlite3_step(stmt);
			sqlite3_finalize(stmt);
			if (rc == SQLITE_DONE) {
				sqlite3_exec(sim_.db, "COMMIT", NULL, NULL, NULL);
				return sim_.redirect("/c");
			}
			sqlite3_exec(sim_.db, "ROLLBACK", NULL, NULL, NULL);
		}

	TemplateWithMenu templ(*this, "Delete contest");
	templ.printRoundPath(*r_path_, "");
	templ << fv.errors() << "<div class=\"form-container\">\n"
		"<h1>Delete contest</h1>\n"
		"<form method=\"post\">\n"
			"<label class=\"field\">Are you sure to delete contest "
				"<a href=\"/c/" << r_path_->round_id << "\">"
				<< htmlSpecialChars(r_path_->contest->name) << "</a>, all "
				"subrounds and submissions?</label>\n"
			"<div class=\"submit-yes-no\">\n"
				"<button class=\"btn-danger\" type=\"submit\" name=\"delete\">"
					"Yes, I'm sure</button>\n"
				"<a class=\"btn\" href=\"/c/" << r_path_->round_id << "/edit\">"
					"No, go back</a>\n"
			"</div>\n"
		"</form>\n"
	"</div>\n";
}

void Sim::Contest::deleteRound() {
	if (!r_path_->admin_access)
		return sim_.error403();

	FormValidator fv(sim_.req_->form_data);
	if (sim_.req_->method == server::HttpRequest::POST)
		if (fv.exist("delete")) {
			sqlite3_stmt *stmt;
			// Delete submissions
			if (sqlite3_prepare_v2(sim_.db,
					"DELETE FROM submissions, submissions_to_rounds "
					"USING submissions INNER JOIN submissions_to_rounds "
					"WHERE parent_round_id=? AND id=submission_id", -1, &stmt,
					NULL))
				return sim_.error500();

			sqlite3_bind_text(stmt, 1, r_path_->round_id.c_str(), -1,
				SQLITE_STATIC);

			sqlite3_exec(sim_.db, "BEGIN", NULL, NULL, NULL);
			int rc = sqlite3_step(stmt);
			sqlite3_finalize(stmt);
			if (rc != SQLITE_DONE) {
				sqlite3_exec(sim_.db, "ROLLBACK", NULL, NULL, NULL);
				return sim_.error500();
			}

			// Delete rounds
			if (sqlite3_prepare_v2(sim_.db, "DELETE FROM rounds "
					"WHERE id=?1 OR parent=?1", -1, &stmt, NULL)) {
				sqlite3_exec(sim_.db, "ROLLBACK", NULL, NULL, NULL);
				return sim_.error500();
			}

			sqlite3_bind_text(stmt, 1, r_path_->round_id.c_str(), -1,
				SQLITE_STATIC);

			rc = sqlite3_step(stmt);
			sqlite3_finalize(stmt);
			if (rc == SQLITE_DONE) {
				sqlite3_exec(sim_.db, "COMMIT", NULL, NULL, NULL);
				return sim_.redirect("/c/" + r_path_->contest->id);
			}
			sqlite3_exec(sim_.db, "ROLLBACK", NULL, NULL, NULL);
		}

	TemplateWithMenu templ(*this, "Delete round");
	templ.printRoundPath(*r_path_, "");
	templ << fv.errors() << "<div class=\"form-container\">\n"
		"<h1>Delete round</h1>\n"
		"<form method=\"post\">\n"
			"<label class=\"field\">Are you sure to delete round <a href=\"/c/"
				<< r_path_->round_id << "\">"
				<< htmlSpecialChars(r_path_->round->name) << "</a>, all "
				"subrounds and submissions?</label>\n"
			"<div class=\"submit-yes-no\">\n"
				"<button class=\"btn-danger\" type=\"submit\" name=\"delete\">"
					"Yes, I'm sure</button>\n"
				"<a class=\"btn\" href=\"/c/" << r_path_->round_id << "/edit\">"
					"No, go back</a>\n"
			"</div>\n"
		"</form>\n"
	"</div>\n";
}

void Sim::Contest::deleteProblem() {
	if (!r_path_->admin_access)
		return sim_.error403();
}

void Sim::Contest::problems(bool admin_view) {
	TemplateWithMenu templ(*this, "Problems");
	templ << "<h1>Problems</h1>";
	templ.printRoundPath(*r_path_, "problems");
	templ.printRoundView(*r_path_, true, admin_view);
}

void Sim::Contest::submit(bool admin_view) {
	if (sim_.session->open() != Sim::Session::OK)
		return sim_.redirect("/login" + sim_.req_->target);

	FormValidator fv(sim_.req_->form_data);
	sqlite3_stmt *stmt;

	if (sim_.req_->method == server::HttpRequest::POST) {
		string solution, problem_round_id;

		// Validate all fields
		fv.validateNotBlank(solution, "solution", "Solution file field");

		fv.validateNotBlank(problem_round_id, "round-id", "Problem");

		if (!isInteger(problem_round_id))
			fv.addError("Wrong problem round id");

		// If all fields are ok
		if (fv.noErrors()) {
			UniquePtr<RoundPath> path;
			const RoundPath* problem_r_path = r_path_;

			if (r_path_->type != PROBLEM) {
				// Get parent rounds of problem round
				path.reset(getRoundPath(problem_round_id));
				if (path.isNull())
					return;

				if (path->type != PROBLEM) {
					fv.addError("Wrong problem round id");
					goto form;
				}

				problem_r_path = path.get();
			}

			string solution_tmp_path = fv.getFilePath("solution");
			struct stat sb;
			stat(solution_tmp_path.c_str(), &sb);

			// Check if solution is too big
			if (sb.st_size > 100 << 10) { // 100kB
				fv.addError("Solution file to big (max 100kB)");
				goto form;
			}

			string current_date = date("%Y-%m-%d %H:%M:%S");
			// Insert submission to `submissions`
			if (sqlite3_prepare_v2(sim_.db, "INSERT INTO submissions "
					"(user_id, problem_id, round_id, parent_round_id, "
						"contest_round_id, submit_time, status, queued) "
					"VALUES(?, ?, ?, ?, ?, ?6, ?, ?6)", -1, &stmt, NULL))
				return sim_.error500();

			sqlite3_bind_text(stmt, 1, sim_.session->user_id.c_str(), -1,
				SQLITE_STATIC);
			sqlite3_bind_text(stmt, 2,
				problem_r_path->problem->problem_id.c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt, 3, problem_r_path->problem->id.c_str(), -1,
				SQLITE_STATIC);
			sqlite3_bind_text(stmt, 4, problem_r_path->round->id.c_str(), -1,
				SQLITE_STATIC);
			sqlite3_bind_text(stmt, 5, problem_r_path->contest->id.c_str(), -1,
				SQLITE_STATIC);
			sqlite3_bind_text(stmt, 6, current_date.c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_int(stmt, 7, WAITING);

			sqlite3_exec(sim_.db, "BEGIN", NULL, NULL, NULL);
			int rc = sqlite3_step(stmt);
			sqlite3_finalize(stmt);
			if (rc != SQLITE_DONE) {
				sqlite3_exec(sim_.db, "ROLLBACK", NULL, NULL, NULL);
				return sim_.error500();
			}

			// Get inserted submission id
			if (sqlite3_prepare_v2(sim_.db, "SELECT LAST_INSERT_ROWID()", -1,
					&stmt, NULL)) {
				sqlite3_exec(sim_.db, "ROLLBACK", NULL, NULL, NULL);
				return sim_.error500();
			}

			if (sqlite3_step(stmt) != SQLITE_ROW) {
				sqlite3_finalize(stmt);
				sqlite3_exec(sim_.db, "ROLLBACK", NULL, NULL, NULL);
				return sim_.error500();
			}

			string submission_id = (const char*)sqlite3_column_text(stmt, 0);
			sqlite3_finalize(stmt);

			// Insert submission to `submissions_to_rounds`
			if (sqlite3_prepare_v2(sim_.db, "INSERT INTO submissions_to_rounds "
					"(submission_id, user_id, round_id, submit_time) "
					"VALUES(?1, ?2, ?3, ?4),(?1, ?2, ?5, ?4),(?1, ?2, ?6, ?4)",
					-1, &stmt, NULL)) {
				sqlite3_exec(sim_.db, "ROLLBACK", NULL, NULL, NULL);
				return sim_.error500();
			}

			sqlite3_bind_text(stmt, 1, submission_id.c_str(), -1,
				SQLITE_STATIC);
			sqlite3_bind_text(stmt, 2, sim_.session->user_id.c_str(), -1,
				SQLITE_STATIC);
			sqlite3_bind_text(stmt, 3, problem_r_path->problem->id.c_str(), -1,
				SQLITE_STATIC);
			sqlite3_bind_text(stmt, 4, current_date.c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt, 5, problem_r_path->round->id.c_str(), -1,
				SQLITE_STATIC);
			sqlite3_bind_text(stmt, 6, problem_r_path->contest->id.c_str(), -1,
				SQLITE_STATIC);

			rc = sqlite3_step(stmt);
			sqlite3_finalize(stmt);
			if (rc != SQLITE_DONE) {
				sqlite3_exec(sim_.db, "ROLLBACK", NULL, NULL, NULL);
				return sim_.error500();
			}

			sqlite3_exec(sim_.db, "COMMIT", NULL, NULL, NULL);
			// Copy solution
			copy(solution_tmp_path, "solutions/" + submission_id + ".cpp");
			// Notify judge-machine
			utime("judge-machine.notify", NULL);

			return sim_.redirect("/s/" + submission_id);
		}
	}

 form:
	TemplateWithMenu templ(*this, "Submit a solution");
	templ.printRoundPath(*r_path_, "");
	string buffer;
	append(buffer) << fv.errors() << "<div class=\"form-container\">\n"
			"<h1>Submit a solution</h1>\n"
			"<form method=\"post\" enctype=\"multipart/form-data\">\n"
				// Round id
				"<div class=\"field-group\">\n"
					"<label>Problem</label>\n"
					"<select name=\"round-id\">";

	// List problems
	string current_date = date("%Y-%m-%d %H:%M:%S");
	if (r_path_->type == CONTEST) {
		// Select subrounds
		// Admin -> All problems from all subrounds
		// Normal -> All problems from subrounds which have begun and
		// have not ended
		if (sqlite3_prepare_v2(sim_.db, admin_view ?
				"SELECT id, name FROM rounds WHERE parent=? ORDER BY item"
				: "SELECT id, name FROM rounds WHERE parent=? AND "
					"(begins IS NULL OR begins<=?2) AND "
					"(ends IS NULL OR ?2<ends) ORDER BY item", -1, &stmt, NULL))
			return sim_.error500();

		sqlite3_bind_text(stmt, 1, r_path_->contest->id.c_str(), -1,
			SQLITE_STATIC);
		if (!admin_view)
			sqlite3_bind_text(stmt, 2, current_date.c_str(), -1, SQLITE_STATIC);

		// Collect results
		vector<Subround> subrounds;
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			subrounds.push_back(Subround());
			subrounds.back().id = (const char*)sqlite3_column_text(stmt, 0);
			subrounds.back().name = (const char*)sqlite3_column_text(stmt, 1);
		}
		sqlite3_finalize(stmt);

		// Select problems
		if (sqlite3_prepare_v2(sim_.db, "SELECT id, parent, name FROM rounds "
				"WHERE grandparent=? ORDER BY item", -1, &stmt, NULL))
			return sim_.error500();

		sqlite3_bind_text(stmt, 1, r_path_->contest->id.c_str(), -1,
			SQLITE_STATIC);

		// (round_id, problems)
		std::map<string, vector<Problem> > problems_table;
		// Fill problems with all subrounds
		for (size_t i = 0; i < subrounds.size(); ++i)
			problems_table[subrounds[i].id];

		// Collect results
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			// Get reference to proper vector<Problem>
			__typeof(problems_table.begin()) it =
				problems_table.find((const char*)sqlite3_column_text(stmt, 1));
			// If problem parent is not visible or database error
			if (it == problems_table.end())
				continue; // Ignore

			vector<Problem>& prob = it->second;
			prob.push_back(Problem());
			prob.back().id = (const char*)sqlite3_column_text(stmt, 0);
			prob.back().parent = (const char*)sqlite3_column_text(stmt, 1);
			prob.back().name = (const char*)sqlite3_column_text(stmt, 2);
		}
		sqlite3_finalize(stmt);

		// For each subround list all problems
		foreach (subround, subrounds) {
			vector<Problem>& prob = problems_table[subround->id];

			foreach (problem, prob)
				append(buffer) << "<option value=\"" << problem->id << "\">"
					<< htmlSpecialChars(problem->name) << " ("
					<< htmlSpecialChars(subround->name) << ")</option>\n";
		}

	// Admin -> All problems
	// Normal -> if round has begun and has not ended
	} else if (r_path_->type == ROUND && (admin_view || (
			r_path_->round->begins <= current_date && // "" <= everything
			(r_path_->round->ends.empty() || current_date < r_path_->round->ends)))) {
		// Select problems
		if (sqlite3_prepare_v2(sim_.db, "SELECT id, name FROM rounds "
				"WHERE parent=? ORDER BY item", -1, &stmt, NULL))
			return sim_.error500();

		sqlite3_bind_text(stmt, 1, r_path_->round->id.c_str(), -1,
			SQLITE_STATIC);

		// List problems
		while (sqlite3_step(stmt) == SQLITE_ROW)
			append(buffer) << "<option value=\""
				<< (const char*)sqlite3_column_text(stmt, 0) << "\">"
				<< htmlSpecialChars((const char*)sqlite3_column_text(stmt, 1))
				<< " (" << htmlSpecialChars(r_path_->round->name)
				<< ")</option>\n";
		sqlite3_finalize(stmt);

	// Admin -> Current problem
	// Normal -> if parent round has begun and has not ended
	} else if (r_path_->type == PROBLEM && (admin_view || (
			r_path_->round->begins <= current_date && // "" <= everything
			(r_path_->round->ends.empty() || current_date < r_path_->round->ends)))) {
		append(buffer) << "<option value=\"" << r_path_->problem->id << "\">"
			<< htmlSpecialChars(r_path_->problem->name) << " ("
			<< htmlSpecialChars(r_path_->round->name) << ")</option>\n";
	}

	if (isSuffix(buffer, "</option>\n"))
		templ << buffer << "</select>"
					"</div>\n"
					// Solution file
					"<div class=\"field-group\">\n"
						"<label>Solution</label>\n"
						"<input type=\"file\" name=\"solution\" required>\n"
					"</div>\n"
					"<input class=\"btn\" type=\"submit\" value=\"Submit\">\n"
				"</form>\n"
			"</div>\n";

	else
		templ << "<p>There are no problems for which you can submit a solution...</p>";
}

void Sim::Contest::submission() {
	if (sim_.session->open() != Session::OK)
		return sim_.redirect("/login" + sim_.req_->target);

	size_t arg_beg = 3;

	// Extract round id
	string submission_id;
	int res_code = strToNum(submission_id, sim_.req_->target, arg_beg, '/');
	if (res_code == -1)
		return sim_.error404();

	arg_beg += res_code + 1;

	// Get submission
	sqlite3_stmt *stmt;
	if (sqlite3_prepare_v2(sim_.db,
			"SELECT user_id, round_id, submit_time, status, score, name "
			"FROM submissions s, problems p WHERE s.id=? AND s.problem_id=p.id",
			-1, &stmt, NULL))
		return sim_.error500();

	sqlite3_bind_text(stmt, 1, submission_id.c_str(), -1, SQLITE_STATIC);

	if (sqlite3_step(stmt) != SQLITE_ROW) {
		sqlite3_finalize(stmt);
		return sim_.error404();
	}

	string submission_user_id = (const char*)sqlite3_column_text(stmt, 0);
	string round_id = (const char*)sqlite3_column_text(stmt, 1);
	string submit_time = (const char*)sqlite3_column_text(stmt, 2);
	int submission_status = sqlite3_column_int(stmt, 3);
	string score = (const char*)sqlite3_column_text(stmt, 4);
	string problem_name = (const char*)sqlite3_column_text(stmt, 5);
	sqlite3_finalize(stmt);

	// Get parent rounds
	delete r_path_;
	r_path_ = getRoundPath(round_id);
	if (r_path_ == NULL)
		return;

	if (!r_path_->admin_access && sim_.session->user_id != submission_user_id)
		return sim_.error403();

	// Check if user forces observer view
	bool admin_view = r_path_->admin_access;
	if (0 == compareTo(sim_.req_->target, arg_beg, '/', "n")) {
		admin_view = false;
		arg_beg += 2;
	}

	// Download solution
	if (0 == compareTo(sim_.req_->target, arg_beg, '/', "download")) {
		sim_.resp_.headers["Content-type"] = "application/text";
		sim_.resp_.headers["Content-Disposition"] = "attchment; filename=" +
			submission_id + ".cpp";

		sim_.resp_.content = "solutions/" + submission_id + ".cpp";
		sim_.resp_.content_type = server::HttpResponse::FILE;

		return;
	}

	TemplateWithMenu templ(*this, "Submission " + submission_id);
	templ.printRoundPath(*r_path_, "");

	// View source
	if (0 == compareTo(sim_.req_->target, arg_beg, '/', "source")) {
		vector<string> args;
		append(args)("./CTH")("solutions/" + submission_id + ".cpp");

		char tmp_filename[] = "/tmp/sim-server-tmp.XXXXXX";
		spawn_opts sopt = {
			-1,
			mkstemp(tmp_filename),
			-1
		};

		spawn(args[0], args, &sopt);
		if (sopt.new_stdout_fd >= 0)
			templ << getFileContents(tmp_filename);

		unlink(tmp_filename);
		if (sopt.new_stdout_fd >= 0)
			while (close(sopt.new_stderr_fd) == -1 && errno == EINTR) {}

		return;
	}

	templ << "<div class=\"submission-info\">\n"
		"<div>\n"
			"<h1>Submission " << submission_id << "</h1>\n"
			"<div>\n"
				"<a class=\"btn-small\" href=\""
					<< sim_.req_->target.substr(0, arg_beg - 1)
					<< "/source\">View source</a>\n"
				"<a class=\"btn-small\" href=\""
					<< sim_.req_->target.substr(0, arg_beg - 1)
					<< "/download\">Download</a>\n"
			"</div>\n"
		"</div>\n"
		"<table style=\"width: 100%\">\n"
			"<thead>\n"
				"<tr>"
					"<th style=\"min-width:120px\">Problem</th>"
					"<th style=\"min-width:150px\">Submission time</th>"
					"<th style=\"min-width:150px\">Status</th>"
					"<th style=\"min-width:90px\">Score</th>"
				"</tr>\n"
			"</thead>\n"
			"<tbody>\n"
				"<tr>"
					"<td>" << htmlSpecialChars(problem_name) << "</td>"
					"<td>" << htmlSpecialChars(submit_time) << "</td>"
					"<td";

	if (submission_status == OK)
		templ << " class=\"ok\"";
	else if (submission_status == ERROR)
		templ << " class=\"wa\"";
	else if (submission_status == COMPILE_ERROR ||
			submission_status == JUDGE_ERROR)
		templ << " class=\"tl-rte\"";

	templ << ">" << submissionStatus(submission_status)
						<< "</td>"
					"<td>" << (admin_view ||
						r_path_->round->full_results.empty() ||
						r_path_->round->full_results <=
							date("%Y-%m-%d %H:%M:%S") ? score : "")
						<< "</td>"
				"</tr>\n"
			"</tbody>\n"
		"</table>\n"
		<< "</div>\n";

	// Show testing report
	vector<string> submission_file =
		getFileByLines("submissions/" + submission_id,
			GFBL_IGNORE_NEW_LINES);

	templ << "<div class=\"results\">";
	if (submission_status == COMPILE_ERROR && submission_file.size() > 0) {
		templ << "<h2>Compilation failed</h2>"
			"<pre class=\"compile-errors\">"
			<< convertStringBack(submission_file[0])
			<< "</pre>";

	} else if (submission_file.size() > 1) {
		if (admin_view || r_path_->round->full_results.empty() ||
				r_path_->round->full_results <= date("%Y-%m-%d %H:%M:%S"))
			templ << "<h2>Final testing report</h2>"
				<< convertStringBack(submission_file[0]);

		templ << "<h2>Initial testing report</h2>"
			<< convertStringBack(submission_file[1]);
	}
	templ << "</div>";
}

void Sim::Contest::submissions(bool admin_view) {
	if (sim_.session->open() != Sim::Session::OK)
		return sim_.redirect("/login" + sim_.req_->target);

	TemplateWithMenu templ(*this, "Submissions");
	templ << "<h1>Submissions</h1>";
	templ.printRoundPath(*r_path_, "submissions");

	templ << "<h3>Submission queue size: ";

	sqlite3_stmt *stmt;
	if (sqlite3_prepare_v2(sim_.db,"SELECT COUNT(*) FROM submissions "
			"WHERE status=?", -1, &stmt, NULL))
		return sim_.error500();

	sqlite3_bind_int(stmt, 1, WAITING);

	if (sqlite3_step(stmt) != SQLITE_ROW) {
		sqlite3_finalize(stmt);
		return sim_.error500();
	}

	templ << (const char*)sqlite3_column_text(stmt, 0) << "</h3>";
	sqlite3_finalize(stmt);

	if (sqlite3_prepare_v2(sim_.db, admin_view ?
			"SELECT s.id, str.submit_time, r2.id, r2.name, r.id, r.name, "
				"s.status, s.score, str.final, str.user_id, u.username "
			"FROM submissions_to_rounds str, submissions s, users u, rounds r, "
				"rounds r2 "
			"WHERE str.submission_id=s.id AND s.round_id=r.id AND "
				"r.parent=r2.id AND str.round_id=? AND str.user_id=u.id "
			"ORDER BY str.submit_time DESC"
		: "SELECT s.id, str.submit_time, r3.id, r3.name, r2.id, r2.name, "
				"s.status, s.score, str.final, r3.full_results "
			"FROM submissions_to_rounds str, submissions s, rounds r, "
				"rounds r2, rounds r3 "
			"WHERE str.submission_id=s.id AND r.id=str.round_id AND "
				"s.round_id=r2.id AND r2.parent=r3.id AND str.round_id=? AND "
				"str.user_id=? "
			"ORDER BY str.submit_time DESC", -1, &stmt, NULL))
		return sim_.error500();

	sqlite3_bind_text(stmt, 1, r_path_->round_id.c_str(), -1, SQLITE_STATIC);
	if (!admin_view)
		sqlite3_bind_text(stmt, 2, sim_.session->user_id.c_str(), -1,
			SQLITE_STATIC);

	int rc = sqlite3_step(stmt);
	if (rc == SQLITE_DONE) {
		sqlite3_finalize(stmt);
		templ << "<p>There are no submissions to show</p>";
		return;

	}
	if (rc != SQLITE_ROW) {
		sqlite3_finalize(stmt);
		return sim_.error500();
	}

	templ << "<table class=\"submissions\">\n"
		"<thead>\n"
			"<tr>"
				<< (admin_view ? "<th class=\"user\">User</th>" : "")
				<< "<th class=\"time\">Submission time</th>"
				"<th class=\"problem\">Problem</th>"
				"<th class=\"status\">Status</th>"
				"<th class=\"score\">Score</th>"
				"<th class=\"final\">Final</th>"
			"</tr>\n"
		"</thead>\n"
		"<tbody>\n";

	struct Local {
		static string statusRow(int status) {
			string ret = "<td";

			if (status == OK)
				ret += " class=\"ok\">";
			else if (status == ERROR)
				ret += " class=\"wa\">";
			else if (status == COMPILE_ERROR || status == JUDGE_ERROR)
				ret += " class=\"tl-rte\">";
			else
				ret += ">";

			return ret.append(submissionStatus(status)).append("</td>");
		}
	};

	string current_date = date("%Y-%m-%d %H:%M:%S");
	do {
		templ << "<tr>";
		// User
		if (admin_view)
			templ << "<td><a href=\"/u/"
				<< (const char*)sqlite3_column_text(stmt, 9) << "\">"
				<< htmlSpecialChars((const char*)sqlite3_column_text(stmt, 10))
				<< "</a></td>";
		// Rest
		templ << "<td><a href=\"/s/"
				<< (const char*)sqlite3_column_text(stmt, 0) << "\">"
				<< (const char*)sqlite3_column_text(stmt, 1) << "</a></td>"
			"<td><a href=\"/c/" << (const char*)sqlite3_column_text(stmt, 2)
					<< "\">"
					<< htmlSpecialChars((const char*)sqlite3_column_text(stmt,
						3)) << "</a> -> <a href=\"/c/"
					<< (const char*)sqlite3_column_text(stmt, 4) << "\">"
					<< htmlSpecialChars((const char*)sqlite3_column_text(stmt,
						5)) << "</a></td>"
				<< Local::statusRow(sqlite3_column_int(stmt, 6))
				<< "<td>" << (admin_view ||
					(const char*)sqlite3_column_text(stmt, 9) <= current_date ?
						(const char*)sqlite3_column_text(stmt, 7) : "")
					<< "</td>"
				"<td>" << (sqlite3_column_int(stmt, 8) ? "Yes" : "") << "</td>"
			"<tr>\n";
	} while (sqlite3_step(stmt) == SQLITE_ROW);
	sqlite3_finalize(stmt);

	templ << "</tbody>\n"
		"</table>\n";
}
