#include "sim_contest_utility.h"
#include "sim_session.h"

#include "../include/db.h"
#include "../include/debug.h"
#include "../include/time.h"

#include <stdexcept>
#include <vector>

#define foreach(i,x) for (__typeof(x.begin()) i = x.begin(), \
	i ##__end = x.end(); i != i ##__end; ++i)

using std::string;
using std::vector;

string Sim::Contest::submissionStatus(int status) {
	if (status == OK)
		return "Initial tests: OK";

	if (status == ERROR)
		return "Initial tests: Error";

	if (status == COMPILE_ERROR)
		return "Compilation failed";

	if (status == JUDGE_ERROR)
		return "Judge error";

	if (status == WAITING)
		return "Pending";

	return "Unknown";
}

string Sim::Contest::convertStringBack(const string& str) {
	string res;
	foreach (i, str) {
		if (*i == '\\') {
			if (*++i == 'n') {
				res += '\n';
				continue;
			}

			--i;
		}

		res += *i;
	}

	return res;
}

Sim::Contest::RoundPath* Sim::Contest::getRoundPath(const string& round_id) {
	UniquePtr<RoundPath> r_path(new RoundPath(round_id));

	struct Local {
		static void copyData(UniquePtr<Round>& r, sqlite3_stmt *stmt) {
			r.reset(new Round);
			r->id = (const char*)sqlite3_column_text(stmt, 0);
			r->parent = unnull(sqlite3_column_text(stmt, 1));
			r->problem_id = unnull(sqlite3_column_text(stmt, 2));
			r->public_access = sqlite3_column_int(stmt, 3);
			r->name = (const char*)sqlite3_column_text(stmt, 4);
			r->owner = (const char*)sqlite3_column_text(stmt, 5);
			r->visible = sqlite3_column_int(stmt, 6);
			r->begins = unnull(sqlite3_column_text(stmt, 7));
			r->ends = unnull(sqlite3_column_text(stmt, 8));
			r->full_results = unnull(sqlite3_column_text(stmt, 9));
		}
	};

	sqlite3_stmt *stmt;
	if (sqlite3_prepare_v2(sim_.db, "SELECT id, parent, problem_id, "
				"public_access, name, owner, visible, begins, ends, "
				"full_results "
			"FROM rounds "
			"WHERE id=?1 OR id=(SELECT parent FROM rounds WHERE id=?1) OR "
				"id=(SELECT grandparent FROM rounds WHERE id=?1)", -1, &stmt,
			NULL)) {
		sim_.error500();
		return NULL;
	}

	sqlite3_bind_text(stmt, 1, round_id.c_str(), -1, SQLITE_STATIC);

	int rc = sqlite3_step(stmt);
	// If round does not exist
	if (rc == SQLITE_DONE) {
		sqlite3_finalize(stmt);
		sim_.error404();
		return NULL;

	}
	if (rc != SQLITE_ROW) {
		sqlite3_finalize(stmt);
		sim_.error500();
		return NULL;
	}


	int rows = 0;
	do {
		++rows;
		if (sqlite3_column_text(stmt, 1) == NULL)
			Local::copyData(r_path->contest, stmt);
		else if (sqlite3_column_text(stmt, 2) == NULL) // problem_id IS NULL
			Local::copyData(r_path->round, stmt);
		else
			Local::copyData(r_path->problem, stmt);
	} while (sqlite3_step(stmt) == SQLITE_ROW);
	sqlite3_finalize(stmt);

	r_path->type = (rows == 1 ? CONTEST : (rows == 2 ? ROUND : PROBLEM));

	// Check rounds hierarchy
	if (r_path->contest.isNull() || (rows > 1 && r_path->round.isNull())
			|| (rows > 2 && r_path->problem.isNull()))
		throw std::runtime_error("Database corruption (rounds hierarchy)");

	// Check access
	r_path->admin_access = isAdmin(sim_, *r_path);
	if (!r_path->admin_access) {
		if (!r_path->contest->public_access) {
			// Check access to contest
			if (sim_.session->open() != Sim::Session::OK) {
				sim_.redirect("/login" + sim_.req_->target);
				return NULL;
			}

			if (sqlite3_prepare_v2(sim_.db, "SELECT user_id "
					"FROM users_to_contests WHERE user_id=? AND contest_id=?",
					-1, &stmt,NULL)) {
				sim_.error500();
				return NULL;
			}

			sqlite3_bind_text(stmt, 1, sim_.session->user_id.c_str(), -1,
				SQLITE_STATIC);
			sqlite3_bind_text(stmt, 2, r_path->contest->id.c_str(), -1,
				SQLITE_STATIC);

			rc = sqlite3_step(stmt);
			sqlite3_finalize(stmt);
			if (rc == SQLITE_DONE) {
				// User is not assigned to this contest
				sim_.error403();
				return NULL;
			}
			if (rc != SQLITE_ROW) {
				sim_.error500();
				return NULL;
			}
		}

		// Check access to round - check if round has begun
		// If begin time is not null and round has not begun, error403
		if (r_path->type != CONTEST && r_path->round->begins.size() &&
				date("%Y-%m-%d %H:%M:%S") < r_path->round->begins) {
			sim_.error403();
			return NULL;
		}
	}

	return r_path.release();
}

bool Sim::Contest::isAdmin(Sim& sim, const RoundPath& r_path) {
	// If is not logged in, he cannot be admin
	if (sim.session->open() != Sim::Session::OK)
		return false;

	// User is the owner of the contest
	if (r_path.contest->owner == sim.session->user_id)
		return true;

	// Check if user has more privileges than the owner
	sqlite3_stmt *stmt;
	if (sqlite3_prepare_v2(sim.db, "SELECT id, type FROM users "
			"WHERE id=? OR id=?", -1, &stmt, NULL))
		throw std::runtime_error("isAdmin(): sqlite3_prepare_v2() failed");

	sqlite3_bind_text(stmt, 1, r_path.contest->owner.c_str(), -1,
		SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, sim.session->user_id.c_str(), -1,
		SQLITE_STATIC);

	int owner_type = 0, user_type = 4;
	for (int i = 0; i < 2 && sqlite3_step(stmt) == SQLITE_ROW; ++i) {
		if (r_path.contest->owner == (const char*)sqlite3_column_text(stmt, 0))
			owner_type = sqlite3_column_int(stmt, 1);
		else
			user_type = sqlite3_column_int(stmt, 1);
	}

	sqlite3_finalize(stmt);
	return owner_type > user_type;
}

Sim::Contest::TemplateWithMenu::TemplateWithMenu(Contest& sim_contest,
	const string& title, const string& styles, const string& scripts)
		: Sim::Template(sim_contest.sim_, title,
			".body{margin-left:190px}" + styles, scripts) {

	*this << "<ul class=\"menu\">\n";

	// Aliases
	string &round_id = sim_contest.r_path_->round_id;
	bool &admin_access = sim_contest.r_path_->admin_access;
	Round *round = sim_contest.r_path_->round.get();

	if (admin_access) {
		*this <<  "<a href=\"/c/add\">Add contest</a>\n"
			"<span>CONTEST ADMINISTRATION</span>\n";

		if (sim_contest.r_path_->type == CONTEST)
			*this << "<a href=\"/c/" << round_id << "/add\">Add round</a>\n"
				"<a href=\"/c/" << round_id << "/edit\">Edit contest</a>\n";

		else if (sim_contest.r_path_->type == ROUND)
			*this << "<a href=\"/c/" << round_id << "/add\">Add problem</a>\n"
				"<a href=\"/c/" << round_id << "/edit\">Edit round</a>\n";

		else // sim_contest.r_path_.type == PROBLEM
			*this << "<a href=\"/c/" << round_id << "/edit\">Edit problem</a>"
				"\n";

		*this << "<a href=\"/c/" << round_id << "\">Dashboard</a>\n"
				"<a href=\"/c/" << round_id << "/problems\">Problems</a>\n"
				"<a href=\"/c/" << round_id << "/submit\">Submit a solution</a>"
					"\n"
				"<a href=\"/c/" << round_id << "/submissions\">Submissions</a>"
					"\n"
				"<span>OBSERVER MENU</span>\n";
	}

	*this << "<a href=\"/c/" << round_id << (admin_access ? "/n" : "")
				<< "\">Dashboard</a>\n"
			"<a href=\"/c/" << round_id << (admin_access ? "/n" : "")
				<< "/problems\">Problems</a>\n";

	string current_date = date("%Y-%m-%d %H:%M:%S");
	if (sim_contest.r_path_->type == CONTEST || (
			(round->begins.empty() || round->begins <= current_date) &&
			(round->ends.empty() || current_date < round->ends)))
		*this << "<a href=\"/c/" << round_id << (admin_access ? "/n" : "")
				<< "/submit\">Submit a solution</a>\n";

	*this << "<a href=\"/c/" << round_id << (admin_access ? "/n" : "")
				<< "/submissions\">Submissions</a>\n"
		"</ul>";
}

void Sim::Contest::TemplateWithMenu::printRoundPath(const RoundPath& r_path,
		const string& page) {
	*this << "<div class=\"round-path\"><a href=\"/c/" << r_path.contest->id <<
		"/" << page << "\">" << htmlSpecialChars(r_path.contest->name)
		<< "</a>";

	if (r_path.type != CONTEST) {
		*this << " -> <a href=\"/c/" << r_path.round->id << "/" << page << "\">"
			<< htmlSpecialChars(r_path.round->name) << "</a>";

		if (r_path.type == PROBLEM)
			*this << " -> <a href=\"/c/" << r_path.problem->id << "/" << page
				<< "\">"
				<< htmlSpecialChars(r_path.problem->name) << "</a>";
	}

	*this << "</div>\n";
}

namespace {

struct SubroundExtended {
	string id, name, item, begins, ends, full_results;
	bool visible;
};

} // anonymous namespace

void Sim::Contest::TemplateWithMenu::printRoundView(const RoundPath& r_path,
		bool link_to_problem_statement, bool admin_view) {
	sqlite3_stmt *stmt;
	if (r_path.type == CONTEST) {
		// Select subrounds
		if (sqlite3_prepare_v2(sim_.db, admin_view ?
				"SELECT id, name, item, visible, begins, ends, "
					"full_results FROM rounds WHERE parent=? ORDER BY item"
				: "SELECT id, name, item, visible, begins, ends, "
					"full_results FROM rounds WHERE parent=? AND "
						"(visible=1 OR begins IS NULL OR begins<=?) "
					"ORDER BY item", -1, &stmt, NULL))
			throw std::runtime_error("printRoundView(): sqlite3_prepare_v2()"
				"failed");

		sqlite3_bind_text(stmt, 1, r_path.contest->id.c_str(), -1,
			SQLITE_STATIC);
		if (!admin_view)
			sqlite3_bind_text(stmt, 2, date("%Y-%m-%d %H:%M:%S").c_str(), -1,
				SQLITE_TRANSIENT); // current date

		// Collect results
		vector<SubroundExtended> subrounds;
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			subrounds.push_back(SubroundExtended());
			subrounds.back().id = (const char*)sqlite3_column_text(stmt, 0);
			subrounds.back().name = (const char*)sqlite3_column_text(stmt, 1);
			subrounds.back().item = (const char*)sqlite3_column_text(stmt, 2);
			subrounds.back().visible = sqlite3_column_int(stmt, 3);
			subrounds.back().begins = unnull(sqlite3_column_text(stmt, 4));
			subrounds.back().ends = unnull(sqlite3_column_text(stmt, 5));
			subrounds.back().full_results =
				unnull(sqlite3_column_text(stmt, 6));
		}
		sqlite3_finalize(stmt);

		// Select problems
		if (sqlite3_prepare_v2(sim_.db, "SELECT id, parent, name FROM rounds "
				"WHERE grandparent=? ORDER BY item", -1, &stmt, NULL))
			throw std::runtime_error("printRoundView(): sqlite3_prepare_v2()"
				"failed");

		sqlite3_bind_text(stmt, 1, r_path.contest->id.c_str(), -1,
			SQLITE_STATIC);


		std::map<string, vector<Problem> > problems; // (round_id, problems)
		// Fill with all subrounds
		for (size_t i = 0; i < subrounds.size(); ++i)
			problems[subrounds[i].id];

		// Collect results
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			// Get reference to proper vector<Problem>
			__typeof(problems.begin()) it =
				problems.find((const char*)sqlite3_column_text(stmt, 1));
			// If problem parent is not visible or database error
			if (it == problems.end())
				continue; // Ignore

			vector<Problem>& prob = it->second;
			prob.push_back(Problem());
			prob.back().id = (const char*)sqlite3_column_text(stmt, 0);
			prob.back().parent = (const char*)sqlite3_column_text(stmt, 1);
			prob.back().name = (const char*)sqlite3_column_text(stmt, 2);
		}
		sqlite3_finalize(stmt);

		// Construct "table"
		*this << "<div class=\"round-view\">\n"
			"<a href=\"/c/" << r_path.contest->id << "\"" << ">"
				<< htmlSpecialChars(r_path.contest->name) << "</a>\n"
			"<div>\n";

		// For each subround list all problems
		for (size_t i = 0; i < subrounds.size(); ++i) {
			// Round
			*this << "<div>\n"
				"<a href=\"/c/" << subrounds[i].id << "\">"
				<< htmlSpecialChars(subrounds[i].name) << "</a>\n";

			// List problems
			vector<Problem>& prob = problems[subrounds[i].id];
			for (size_t j = 0; j < prob.size(); ++j) {
				*this << "<a href=\"/c/" << prob[j].id;

				if (link_to_problem_statement)
					*this << "/statement";

				*this << "\">" << htmlSpecialChars(prob[j].name)
					<< "</a>\n";
			}
			*this << "</div>\n";
		}
		*this << "</div>\n"
			"</div>\n";

	} else if (r_path.type == ROUND) {
		// Construct "table"
		*this << "<div class=\"round-view\">\n"
			"<a href=\"/c/" << r_path.contest->id << "\"" << ">"
				<< htmlSpecialChars(r_path.contest->name) << "</a>\n"
			"<div>\n";
		// Round
		*this << "<div>\n"
			"<a href=\"/c/" << r_path.round->id << "\">"
				<< htmlSpecialChars(r_path.round->name) << "</a>\n";

		// Select problems
		if (sqlite3_prepare_v2(sim_.db, "SELECT id, name FROM rounds "
				"WHERE parent=? ORDER BY item", -1, &stmt, NULL))
			throw std::runtime_error("printRoundView(): sqlite3_prepare_v2()"
				"failed");

		sqlite3_bind_text(stmt, 1, r_path.round->id.c_str(), -1, SQLITE_STATIC);

		// List problems
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			*this << "<a href=\"/c/"
				<< (const char*)sqlite3_column_text(stmt, 0);

			if (link_to_problem_statement)
				*this << "/statement";

			*this << "\">"
				<< htmlSpecialChars((const char*)sqlite3_column_text(stmt, 1))
				<< "</a>\n";
		}
		sqlite3_finalize(stmt);
		*this << "</div>\n"
			"</div>\n"
			"</div>\n";

	} else { // r_path.type == PROBLEM
		// Construct "table"
		*this << "<div class=\"round-view\">\n"
			"<a href=\"/c/" << r_path.contest->id << "\"" << ">"
				<< htmlSpecialChars(r_path.contest->name) << "</a>\n"
			"<div>\n";
		// Round
		*this << "<div>\n"
			"<a href=\"/c/" << r_path.round->id << "\">"
				<< htmlSpecialChars(r_path.round->name) << "</a>\n"
			"<a href=\"/c/" << r_path.problem->id;

		if (link_to_problem_statement)
			*this << "/statement";

		*this << "\">" << htmlSpecialChars(r_path.problem->name) << "</a>\n"
				"</div>\n"
			"</div>\n"
			"</div>\n";
	}
}
