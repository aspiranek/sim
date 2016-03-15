#include "sim_contest_utility.h"
#include "sim_session.h"

#include <simlib/logger.h>
#include <simlib/time.h>

using std::string;
using std::vector;

string Sim::Contest::submissionStatus(const string& status) {
	if (status == "ok")
		return "Initial tests: OK";

	if (status == "error")
		return "Initial tests: Error";

	if (status == "c_error")
		return "Compilation failed";

	if (status == "judge_error")
		return "Judge error";

	if (status == "waiting")
		return "Pending";

	return "Unknown";
}

Sim::Contest::RoundPath* Sim::Contest::getRoundPath(const string& round_id) {
	std::unique_ptr<RoundPath> r_path(new RoundPath(round_id));

	try {
		DB::Statement stmt = sim_.db_conn.prepare(
			"SELECT id, parent, problem_id, name, owner, is_public, visible, "
				"show_ranking, begins, ends, full_results "
			"FROM rounds "
			"WHERE id=? OR id=(SELECT parent FROM rounds WHERE id=?) OR "
				"id=(SELECT grandparent FROM rounds WHERE id=?)");
		stmt.setString(1, round_id);
		stmt.setString(2, round_id);
		stmt.setString(3, round_id);

		DB::Result res = stmt.executeQuery();

		auto copyDataTo = [&](std::unique_ptr<Round>& r) {
			r.reset(new Round);
			r->id = res[1];
			r->parent = res[2];
			r->problem_id = res[3];
			r->name = res[4];
			r->owner = res[5];
			r->is_public = res.getBool(6);
			r->visible = res.getBool(7);
			r->show_ranking = res.getBool(8);
			r->begins = res[9];
			r->ends = res[10];
			r->full_results = res[11];
		};

		int rows = res.rowCount();
		// If round does not exist
		if (rows == 0) {
			sim_.error404();
			return nullptr;
		}

		r_path->type = (rows == 1 ? CONTEST : (rows == 2 ? ROUND : PROBLEM));

		while (res.next()) {
			if (res.isNull(2))
				copyDataTo(r_path->contest);
			else if (res.isNull(3)) // problem_id IS NULL
				copyDataTo(r_path->round);
			else
				copyDataTo(r_path->problem);
		}

		// Check rounds hierarchy
		if (!r_path->contest || (rows > 1 && !r_path->round)
				|| (rows > 2 && !r_path->problem))
			throw std::runtime_error(concat("Database error: corrupt hierarchy "
				"of rounds (id: ", round_id, ")"));

		// Check access
		r_path->admin_access = isAdmin(sim_, *r_path); // TODO: get data in above query!
		if (!r_path->admin_access) {
			if (!r_path->contest->is_public) {
				// Check access to contest
				if (sim_.session->open() != Sim::Session::OK) {
					sim_.redirect("/login" + sim_.req_->target);
					return nullptr;
				}

				stmt = sim_.db_conn.prepare("SELECT user_id "
					"FROM users_to_contests WHERE user_id=? AND contest_id=?");
				stmt.setString(1, sim_.session->user_id);
				stmt.setString(2, r_path->contest->id);

				res = stmt.executeQuery();
				if (!res.next()) {
					// User is not assigned to this contest
					sim_.error403();
					return nullptr;
				}
			}

			// Check access to round - check if round has begun
			// If begin time is not null and round has not begun, error403
			if (r_path->type != CONTEST && r_path->round->begins.size() &&
					date("%Y-%m-%d %H:%M:%S") < r_path->round->begins) {
				sim_.error403();
				return nullptr;
			}
		}

	} catch (const std::exception& e) {
		errlog("Caught exception: ", __FILE__, ':', toString(__LINE__), " -> ",
			e.what());
		sim_.error500();
		return nullptr;
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

	try {
		// Check if user has more privileges than the owner
		DB::Statement stmt = sim.db_conn.prepare(
			"SELECT id, type FROM users WHERE id=? OR id=?");
		stmt.setString(1, r_path.contest->owner);
		stmt.setString(2, sim.session->user_id);

		DB::Result res = stmt.executeQuery();
		int owner_type = 0, user_type = 4;
		for (int i = 0; i < 2 && res.next(); ++i) {
			if (res[1] == r_path.contest->owner)
				owner_type = res.getUInt(2);
			else
				user_type = res.getUInt(2);
		}

		return owner_type > user_type;

	} catch (const std::exception& e) {
		errlog("Caught exception: ", __FILE__, ':', toString(__LINE__), " -> ",
			e.what());
	}

	return false;
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

		// Adding
		*this << "<a href=\"/c/" << sim_contest.r_path_->contest->id
			<< "/add\">Add round</a>\n";
		if (sim_contest.r_path_->type >= ROUND)
			*this << "<a href=\"/c/" << sim_contest.r_path_->round->id
				<< "/add\">Add problem</a>\n";

		// Editing
		*this << "<a href=\"/c/" << sim_contest.r_path_->contest->id
			<< "/edit\">Edit contest</a>\n";
		if (sim_contest.r_path_->type >= ROUND)
			*this << "<a href=\"/c/" << sim_contest.r_path_->round->id
				<< "/edit\">Edit round</a>\n";
		if (sim_contest.r_path_->type == PROBLEM)
			*this << "<a href=\"/c/" << round_id
				<< "/edit\">Edit problem</a>\n";

		*this << "<hr/>"
				"<a href=\"/c/" << sim_contest.r_path_->contest->id
					<< "\">Contest dashboard</a>\n"
				"<a href=\"/c/" << sim_contest.r_path_->contest->id
					<< "/problems\">Problems</a>\n"
				"<a href=\"/c/" << sim_contest.r_path_->contest->id
					<< "/files\">Files</a>\n"
				"<a href=\"/c/" << round_id << "/submit\">Submit a solution</a>"
					"\n"
				"<a href=\"/c/" << sim_contest.r_path_->contest->id
					<< "/submissions\">Submissions</a>\n"
				"<a href=\"/c/" << round_id
					<< "/submissions\">Local submissions</a>\n"
				"<a href=\"/c/" << sim_contest.r_path_->contest->id
					<< "/ranking\">Ranking</a>\n"
				"<span>OBSERVER MENU</span>\n";
	}

	*this << "<a href=\"/c/" << sim_contest.r_path_->contest->id
			<< (admin_access ? "/n" : "") << "\">Contest dashboard</a>\n"
		"<a href=\"/c/" << sim_contest.r_path_->contest->id
			<< (admin_access ? "/n" : "") << "/problems\">Problems</a>\n"
		"<a href=\"/c/" << sim_contest.r_path_->contest->id
			<< (admin_access ? "/n" : "") << "/files\">Files</a>\n";

	string current_date = date("%Y-%m-%d %H:%M:%S");
	if (sim_contest.r_path_->type == CONTEST || (
			(round->begins.empty() || round->begins <= current_date) &&
			(round->ends.empty() || current_date < round->ends)))
		*this << "<a href=\"/c/" << round_id << (admin_access ? "/n" : "")
			<< "/submit\">Submit a solution</a>\n";

	*this << "<a href=\"/c/" << sim_contest.r_path_->contest->id
		<< (admin_access ? "/n" : "") << "/submissions\">Submissions</a>\n";

	*this << "<a href=\"/c/" << round_id << (admin_access ? "/n" : "")
		<< "/submissions\">Local submissions</a>\n";

	if (sim_contest.r_path_->contest->show_ranking)
		*this << "<a href=\"/c/" << sim_contest.r_path_->contest->id
			<< (admin_access ? "/n" : "") << "/ranking\">Ranking</a>\n";

	*this << "</ul>";
}

void Sim::Contest::TemplateWithMenu::printRoundPath(const RoundPath& r_path,
		const string& page, bool force_normal) {
	*this << "<div class=\"round-path\"><a href=\"/c/" << r_path.contest->id
		<< (force_normal ? "/n/" : "/") << page << "\">"
		<< htmlSpecialChars(r_path.contest->name) << "</a>";

	if (r_path.type != CONTEST) {
		*this << " -> <a href=\"/c/" << r_path.round->id
			<< (force_normal ? "/n/" : "/") << page << "\">"
			<< htmlSpecialChars(r_path.round->name) << "</a>";

		if (r_path.type == PROBLEM)
			*this << " -> <a href=\"/c/" << r_path.problem->id
				<< (force_normal ? "/n/" : "/") << page << "\">"
				<< htmlSpecialChars(r_path.problem->name) << "</a>";
	}

	*this << "</div>\n";
}

void Sim::Contest::TemplateWithMenu::printRoundView(const RoundPath& r_path,
		bool link_to_problem_statement, bool admin_view) {
	try {
		if (r_path.type == CONTEST) {
			// Select subrounds
			DB::Statement stmt = sim_.db_conn.prepare(admin_view ?
					"SELECT id, name, item, visible, begins, ends, "
						"full_results FROM rounds WHERE parent=? ORDER BY item"
				: "SELECT id, name, item, visible, begins, ends, full_results "
					"FROM rounds WHERE parent=? AND "
						"(visible IS TRUE OR begins IS NULL OR begins<=?) "
					"ORDER BY item");
			stmt.setString(1, r_path.contest->id);
			if (!admin_view)
				stmt.setString(2, date("%Y-%m-%d %H:%M:%S")); // current date

			struct SubroundExtended {
				string id, name, item, begins, ends, full_results;
				bool visible;
			};

			DB::Result res = stmt.executeQuery();
			vector<SubroundExtended> subrounds;
			// For performance
			subrounds.reserve(res.rowCount());

			// Collect results
			while (res.next()) {
				subrounds.emplace_back();
				subrounds.back().id = res[1];
				subrounds.back().name = res[2];
				subrounds.back().item = res[3];
				subrounds.back().visible = res.getBool(4);
				subrounds.back().begins = res[5];
				subrounds.back().ends = res[6];
				subrounds.back().full_results = res[7];
			}

			// Select problems
			stmt = sim_.db_conn.prepare("SELECT id, parent, name FROM rounds "
					"WHERE grandparent=? ORDER BY item");
			stmt.setString(1, r_path.contest->id);

			res = stmt.executeQuery();
			std::map<string, vector<Problem> > problems; // (round_id, problems)

			// Fill with all subrounds
			for (size_t i = 0; i < subrounds.size(); ++i)
				problems[subrounds[i].id];

			// Collect results
			while (res.next()) {
				// Get reference to proper vector<Problem>
				auto it = problems.find(res[2]);
				// If problem parent is not visible or database error
				if (it == problems.end())
					continue; // Ignore

				vector<Problem>& prob = it->second;
				prob.emplace_back();
				prob.back().id = res[1];
				prob.back().parent = res[2];
				prob.back().name = res[3];
			}

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
			DB::Statement stmt = sim_.db_conn.prepare("SELECT id, name "
				"FROM rounds WHERE parent=? ORDER BY item");
			stmt.setString(1, r_path.round->id);

			// List problems
			DB::Result res = stmt.executeQuery();
			while (res.next()) {
				*this << "<a href=\"/c/" << res[1];

				if (link_to_problem_statement)
					*this << "/statement";

				*this << "\">" << htmlSpecialChars(StringView(
						res[2])) << "</a>\n";
			}
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

	} catch (const std::exception& e) {
		errlog("Caught exception: ", __FILE__, ':', toString(__LINE__), " -> ",
			e.what());
	}
}
