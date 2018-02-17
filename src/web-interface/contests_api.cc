#include "sim.h"

#include <sim/utilities.h>

using MySQL::bind_arg;

void Sim::append_contest_actions_str() {
	STACK_UNWINDING_MARK;

	using PERM = ContestPermissions;

	append('"');
	if (uint(contests_perms & (PERM::ADD_PUBLIC | PERM::ADD_PRIVATE)))
		append('a');
	if (uint(contests_perms & PERM::MAKE_PUBLIC))
		append('M');
	if (uint(contests_perms & PERM::VIEW))
		append('v');
	if (uint(contests_perms & PERM::PARTICIPATE))
		append('p');
	if (uint(contests_perms & PERM::ADMIN))
		append('A');
	if (uint(contests_perms & PERM::EDIT_OWNERS))
		append('O');
	if (uint(contests_perms & PERM::DELETE))
		append('D');
	append('"');
}

static constexpr inline const char* user_mode_to_json(ContestUserMode cum) {
	switch (cum) {
	case ContestUserMode::IS_NULL: return "null";
	case ContestUserMode::CONTESTANT: return "\"contestant\"";
	case ContestUserMode::MODERATOR: return "\"moderator\"";
	case ContestUserMode::OWNER: return "\"owner\"";
	}

	return "unknown";
};

void Sim::api_contests() {
	STACK_UNWINDING_MARK;

	using PERM = ContestPermissions;
	using CUM = ContestUserMode;

	session_open();

	InplaceBuff<512> qfields, qwhere;
	qfields.append("SELECT c.id, c.name, c.is_public, cu.mode");
	qwhere.append(" FROM contests c"
		" LEFT JOIN contest_users cu ON cu.contest_id=c.id AND cu.user_id=",
			(session_is_open ? session_user_id : StringView("''")));

	auto qwhere_append = [&, where_was_added = false](auto&&... args) mutable {
		if (where_was_added)
			qwhere.append(" AND ", std::forward<decltype(args)>(args)...);
		else {
			qwhere.append(" WHERE ", std::forward<decltype(args)>(args)...);
			where_was_added = true;
		}
	};

	// Get the overall permissions to the contests list
	contests_perms = contests_get_permissions();
	// Choose contests to select
	if (uint(~contests_perms & PERM::VIEW_ALL)) {
		throw_assert(uint(contests_perms & PERM::VIEW_PUBLIC));
		if (session_is_open)
			qwhere_append("(c.is_public=true OR cu.mode IS NOT NULL)");

		else
			qwhere_append("c.is_public=true");
	}

	// Process restrictions
	StringView next_arg = url_args.extractNextArg();
	for (uint mask = 0; next_arg.size(); next_arg = url_args.extractNextArg()) {
		constexpr uint ID_COND = 1;
		constexpr uint PUBLIC_COND = 2;

		auto arg = decodeURI(next_arg);
		char cond = arg[0];
		StringView arg_id = StringView{arg}.substr(1);

		// Is public
		if (cond == 'p' and ~mask & PUBLIC_COND) {
			if (arg_id == "Y")
				qwhere_append("c.is_public=true");
			else if (arg_id == "N")
				qwhere_append("c.is_public=false");
			else
				return api_error400(concat("Invalid is_public condition: ",
					arg_id));

			mask |= PUBLIC_COND;
			continue;
		}

		if (not isDigit(arg_id))
			return api_error400();

		// conditional
		if (isIn(cond, {'=', '<', '>'}) and ~mask & ID_COND) {
			qwhere_append("c.id", arg);
			mask |= ID_COND;

		} else
			return api_error400();

	}

	// Execute query
	qfields.append(qwhere, " ORDER BY c.id DESC LIMIT 50");
	auto res = mysql.query(qfields);

	resp.headers["content-type"] = "text/plain; charset=utf-8";
	append("[");
	while (res.next()) {
		StringView cid = res[0];
		StringView name = res[1];
		bool is_public = strtoull(res[2]);
		CUM umode = (res.is_null(3) ? CUM::IS_NULL : CUM(strtoull(res[3])));

		contests_perms = contests_get_permissions(is_public, umode);

		// Id
		append("\n[", cid, ",");
		// Name
		append(jsonStringify(name), ',');
		// Is public
		append(is_public ? "true," : "false,");

		// Contest user mode
		append(user_mode_to_json(umode));

		// Append what buttons to show
		append(',');
		append_contest_actions_str();
		append("],");
	}

	if (resp.content.back() == ',')
		--resp.content.size;

	append("\n]");
}

void Sim::api_contest() {
	STACK_UNWINDING_MARK;

	using PERM = ContestPermissions;
	using CUM = ContestUserMode;

	session_open();
	contests_perms = contests_get_permissions();

	StringView next_arg = url_args.extractNextArg();
	if (next_arg == "add") {
		return api_contest_add();

	} else if (next_arg.empty()) {
		return api_error400();

	// Select by contest round id
	} else if (next_arg[0] == 'r' and isDigit(next_arg.substr(1))) {
		contests_crid = next_arg.substr(1);
		return api_contest_round();

	// Select by contest problem id
	} else if (next_arg[0] == 'p' and isDigit(next_arg.substr(1))) {
		contests_cpid = next_arg.substr(1);
		return api_contest_problem();


	} else if (not (next_arg[0] == 'c' and isDigit(next_arg.substr(1))))
		return api_error404();

	// Select by contest id
	contests_cid = next_arg.substr(1);
	auto stmt = mysql.prepare("SELECT c.name, c.is_public, cu.mode"
		" FROM contests c"
		" LEFT JOIN contest_users cu ON cu.contest_id=c.id AND cu.user_id=?"
		" WHERE c.id=?");
	stmt.bindAndExecute(
		(session_is_open ? session_user_id : StringView()), contests_cid);

	bool is_public;
	my_bool umode_is_null;
	InplaceBuff<meta::max(CONTEST_NAME_MAX_LEN, CONTEST_ROUND_NAME_MAX_LEN,
		CONTEST_PROBLEM_NAME_MAX_LEN)> name;
	std::underlying_type_t<CUM> umode_u;
	stmt.res_bind_all(name, is_public, bind_arg(umode_u, umode_is_null));
	if (not stmt.next())
		return api_error404();

	CUM umode = (umode_is_null ? CUM::IS_NULL : CUM(umode_u));
	contests_perms = contests_get_permissions(is_public, umode);

	next_arg = url_args.extractNextArg();
	if (next_arg == "ranking")
		return api_contest_ranking("contest_id", contests_cid);
	else if (next_arg == "edit")
		return api_contest_edit(is_public);
	else if (next_arg == "add_round")
		return api_contest_round_add();
	else if (not next_arg.empty())
		return api_error400();

	if (uint(~contests_perms & PERM::VIEW))
		return api_error403();

	// Append contest
	append("[\n[", contests_cid, ',',
		jsonStringify(name),
		(is_public ? ",true," : ",false,"),
		user_mode_to_json(umode), ',');
	append_contest_actions_str();

	decltype(mysql_date()) curr_date;

	// Rounds
	append("],\n[");
	if (uint(contests_perms & PERM::ADMIN)) {
		stmt = mysql.prepare("SELECT id, name, item, ranking_exposure,"
				" begins, full_results, ends FROM contest_rounds"
			" WHERE contest_id=?");
		stmt.bindAndExecute(contests_cid);
	} else {
		stmt = mysql.prepare("SELECT id, name, item, ranking_exposure,"
				" begins, full_results, ends FROM contest_rounds"
			" WHERE contest_id=? AND begins<=?");
		curr_date = mysql_date();
		stmt.bindAndExecute(contests_cid, curr_date);
	}

	uint item;
	my_bool ranking_exp_is_null, full_res_is_null, ends_is_null;
	InplaceBuff<20> ranking_exposure, begins, full_results, ends;
	stmt.res_bind_all(contests_crid, name, item,
		bind_arg(ranking_exposure, ranking_exp_is_null), begins,
		bind_arg(full_results, full_res_is_null), bind_arg(ends, ends_is_null));

	while (stmt.next()) {
		append("\n[", contests_crid, ',',
			jsonStringify(name), ',',
			item, ',');

		if (ranking_exp_is_null)
			append("null,");
		else
			append('"', ranking_exposure, "\",");

		append('"', begins, "\",");

		if (full_res_is_null)
			append("null,");
		else
			append('"', full_results, "\",");

		if (ends_is_null)
			append("null],");
		else
			append('"', ends, "\"],");
	}

	if (resp.content.back() == ',')
		--resp.content.size;

	// Problems
	append("\n],\n[");
	if (uint(contests_perms & PERM::ADMIN)) {
		stmt = mysql.prepare("SELECT cp.id, cp.contest_round_id,"
				" cp.problem_id, p.label, cp.name, cp.item,"
				" cp.final_selecting_method"
			" FROM contest_problems cp"
			" LEFT JOIN problems p ON p.id=cp.problem_id"
			" WHERE cp.contest_id=?");
		stmt.bindAndExecute(contests_cid);

	} else {
		stmt = mysql.prepare("SELECT cp.id, cp.contest_round_id,"
				" cp.problem_id, p.label, cp.name, cp.item,"
				" cp.final_selecting_method"
			" FROM contest_problems cp"
			" JOIN contest_rounds cr ON cr.id=cp.contest_round_id"
				" AND cr.begins<=?"
			" LEFT JOIN problems p ON p.id=cp.problem_id"
			" WHERE cp.contest_id=?");
		stmt.bindAndExecute(curr_date, contests_cid);
	}

	uint64_t problem_id;
	uint final_selecting_method;
	InplaceBuff<PROBLEM_LABEL_MAX_LEN> problem_label;

	stmt.res_bind_all(contests_cpid, contests_crid, problem_id,
		problem_label, name, item, final_selecting_method);

	while (stmt.next()) {
		append("\n[", contests_cpid, ',',
			contests_crid, ',',
			problem_id, ',',
			jsonStringify(problem_label), ',',
			jsonStringify(name), ',',
			item, ',',
			final_selecting_method, "],");
	}

	if (resp.content.back() == ',')
		--resp.content.size;
	append("\n]\n]");
}

void Sim::api_contest_round() {
	STACK_UNWINDING_MARK;

	using PERM = ContestPermissions;
	using CUM = ContestUserMode;

	auto stmt = mysql.prepare("SELECT c.id, c.name, c.is_public, cr.name,"
			" cr.item, cr.ranking_exposure, cr.begins, cr.full_results,"
			" cr.ends, cu.mode"
		" FROM contest_rounds cr "
		" STRAIGHT_JOIN contests c ON c.id=cr.contest_id"
		" LEFT JOIN contest_users cu ON cu.contest_id=c.id AND cu.user_id=?"
		" WHERE cr.id=?");

	stmt.bindAndExecute((session_is_open ? session_user_id : StringView()),
		contests_crid);

	bool is_public;
	my_bool ranking_exp_is_null, full_res_is_null, ends_is_null, umode_is_null;
	InplaceBuff<20> ranking_exposure, begins, full_results, ends;
	std::underlying_type_t<CUM> umode_u;
	InplaceBuff<CONTEST_NAME_MAX_LEN> cname;
	InplaceBuff<meta::max(CONTEST_ROUND_NAME_MAX_LEN,
		CONTEST_PROBLEM_NAME_MAX_LEN)> name;
	uint item;
	stmt.res_bind_all(contests_cid, cname, is_public, name, item,
		bind_arg(ranking_exposure, ranking_exp_is_null), begins,
		bind_arg(full_results, full_res_is_null), bind_arg(ends, ends_is_null),
		bind_arg(umode_u, umode_is_null));
	if (not stmt.next())
		return api_error404();

	CUM umode = (umode_is_null ? CUM::IS_NULL : CUM(umode_u));
	contests_perms = contests_get_permissions(is_public, umode);

	if (uint(~contests_perms & PERM::VIEW))
		return api_error403(); // Could not participate

	if (begins > mysql_date() and uint(~contests_perms & PERM::ADMIN))
		return api_error403(); // Round has not begun yet

	StringView next_arg = url_args.extractNextArg();
	if (next_arg == "ranking")
		return api_contest_ranking("contest_round_id", contests_crid);
	else if (next_arg == "attach_problem")
		return api_contest_problem_add();
	else if (next_arg == "edit")
		return api_contest_round_edit();
	else if (not next_arg.empty())
		return api_error404();

	// Append contest
	append("[\n[", contests_cid, ',',
		jsonStringify(cname),
		(is_public ? ",true," : ",false,"),
		user_mode_to_json(umode), ',');
	append_contest_actions_str();

	// Round
	append("],\n[\n[", contests_crid, ',',
		jsonStringify(name), ',',
		item, ',');

	if (ranking_exp_is_null)
		append("null,");
	else
		append('"', ranking_exposure, "\",");

	append('"', begins, "\",");

	if (full_res_is_null)
		append("null,");
	else
		append('"', full_results, "\",");

	if (ends_is_null)
		append("null]");
	else
		append('"', ends, "\"]");

	// Problems
	append("\n],\n[");
	stmt = mysql.prepare("SELECT cp.id, cp.problem_id, p.label, cp.name,"
			" cp.item, cp.final_selecting_method"
		" FROM contest_problems cp"
		" LEFT JOIN problems p ON p.id=cp.problem_id"
		" WHERE cp.contest_round_id=?");
	stmt.bindAndExecute(contests_crid);

	uint64_t problem_id;
	uint final_selecting_method;
	InplaceBuff<PROBLEM_LABEL_MAX_LEN> problem_label;

	stmt.res_bind_all(contests_cpid, problem_id, problem_label, name, item,
		final_selecting_method);

	while (stmt.next()) {
		append("\n[", contests_cpid, ',',
			contests_crid, ',',
			problem_id, ',',
			jsonStringify(problem_label), ',',
			jsonStringify(name), ',',
			item, ',',
			final_selecting_method, "],");
	}

	if (resp.content.back() == ',')
		--resp.content.size;
	append("\n]\n]");
}

void Sim::api_contest_problem() {
	STACK_UNWINDING_MARK;

	using PERM = ContestPermissions;
	using CUM = ContestUserMode;

	// Check permissions to the contest
	auto stmt = mysql.prepare("SELECT c.id, c.name, c.is_public, cr.id,"
			" cr.name, cr.item, cr.ranking_exposure, cr.begins,"
			" cr.full_results, cr.ends, cp.problem_id, p.label, cp.name,"
			" cp.item, cp.final_selecting_method, cu.mode"
		" FROM contest_problems cp"
		" STRAIGHT_JOIN contest_rounds cr ON cr.id=cp.contest_round_id"
		" STRAIGHT_JOIN contests c ON c.id=cp.contest_id"
		" LEFT JOIN problems p ON p.id=cp.problem_id"
		" LEFT JOIN contest_users cu ON cu.contest_id=c.id AND cu.user_id=?"
		" WHERE cp.id=?");
	stmt.bindAndExecute((session_is_open ? session_user_id : StringView()),
		contests_cpid);

	bool is_public;
	my_bool rranking_exp_is_null, rfull_res_is_null, rends_is_null,
		umode_is_null;
	InplaceBuff<20> rranking_exposure, rbegins, rfull_results, rends;
	std::underlying_type_t<CUM> umode_u;
	InplaceBuff<CONTEST_NAME_MAX_LEN> cname;
	InplaceBuff<CONTEST_ROUND_NAME_MAX_LEN> rname;
	InplaceBuff<CONTEST_PROBLEM_NAME_MAX_LEN> pname;
	decltype(problems_pid) problem_id;
	uint ritem, pitem, final_selecting_method;
	InplaceBuff<PROBLEM_LABEL_MAX_LEN> problem_label;

	stmt.res_bind_all(contests_cid, cname, is_public, contests_crid, rname,
		ritem, bind_arg(rranking_exposure, rranking_exp_is_null), rbegins,
		bind_arg(rfull_results, rfull_res_is_null),
		bind_arg(rends, rends_is_null), problem_id, problem_label, pname, pitem,
		final_selecting_method, bind_arg(umode_u, umode_is_null));
	if (not stmt.next())
		return api_error404();

	CUM umode = (umode_is_null ? CUM::IS_NULL : CUM(umode_u));
	contests_perms = contests_get_permissions(is_public, umode);

	if (uint(~contests_perms & PERM::VIEW))
		return api_error403(); // Could not participate

	if (rbegins > mysql_date() and uint(~contests_perms & PERM::ADMIN))
		return api_error403(); // Round has not begun yet

	StringView next_arg = url_args.extractNextArg();
	if (next_arg == "statement")
		return api_contest_problem_statement(problem_id);
	else if (next_arg == "ranking")
		return api_contest_ranking("contest_problem_id", contests_cpid);
	else if (not next_arg.empty())
		return api_error404();

	// Append contest
	append("[\n[", contests_cid, ',',
		jsonStringify(cname),
		(is_public ? ",true," : ",false,"),
		user_mode_to_json(umode), ',');
	append_contest_actions_str();

	// Round
	append("],\n[\n[", contests_crid, ',',
		jsonStringify(rname), ',',
		ritem, ',');

	if (rranking_exp_is_null)
		append("null,");
	else
		append('"', rranking_exposure, "\",");

	append('"', rbegins, "\",");

	if (rfull_res_is_null)
		append("null,");
	else
		append('"', rfull_results, "\",");

	if (rends_is_null)
		append("null]");
	else
		append('"', rends, "\"]");

	// Problem
	append("\n],\n[\n[", contests_cpid, ',',
		contests_crid, ',',
		problem_id, ',',
		jsonStringify(problem_label), ',',
		jsonStringify(pname), ',',
		pitem, ',',
		final_selecting_method, "]\n]\n]");
}

void Sim::api_contest_add() {
	STACK_UNWINDING_MARK;

	using PERM = ContestPermissions;

	if (uint(~contests_perms & (PERM::ADD_PRIVATE | PERM::ADD_PUBLIC)))
		return api_error403();

	// Validate fields
	StringView name;
	form_validate_not_blank(name, "name", "Contest's name",
		CONTEST_NAME_MAX_LEN);

	bool is_public = request.form_data.exist("public");
	if (is_public and uint(~contests_perms & PERM::ADD_PUBLIC))
		add_notification("error",
			"You have no permissions to add a public contest");

	if (not is_public and uint(~contests_perms & PERM::ADD_PRIVATE))
		add_notification("error",
			"You have no permissions to add a private contest");

	if (form_validation_error)
		return api_error400(notifications);

	// Add contest
	auto stmt = mysql.prepare("INSERT contests(name, is_public) VALUES(?, ?)");
	stmt.bindAndExecute(name, is_public);

	auto contest_id = stmt.insert_id();
	// Add user to owners
	stmt = mysql.prepare("INSERT contest_users(user_id, contest_id, mode)"
		" VALUES(?, ?, " CU_MODE_OWNER_STR ")");
	stmt.bindAndExecute(session_user_id, contest_id);

	append(contest_id);
}

void Sim::api_contest_edit(bool is_public) {
	STACK_UNWINDING_MARK;

	using PERM = ContestPermissions;

	if (uint(~contests_perms & PERM::ADMIN))
		return api_error403();

	// Validate fields
	StringView name;
	form_validate_not_blank(name, "name", "Contest's name",
		CONTEST_NAME_MAX_LEN);

	bool will_be_public = request.form_data.exist("public");
	if (will_be_public and not is_public and
		uint(~contests_perms & PERM::MAKE_PUBLIC))
	{
		add_notification("error",
			"You have no permissions to make this contest public");
	}

	if (form_validation_error)
		return api_error400(notifications);

	// Add contest
	auto stmt = mysql.prepare("UPDATE contests SET name=?, is_public=?"
		" WHERE id=?");
	stmt.bindAndExecute(name, will_be_public, contests_cid);
}

void Sim::api_contest_round_add() {
	STACK_UNWINDING_MARK;

	using PERM = ContestPermissions;

	if (uint(~contests_perms & PERM::ADMIN))
		return api_error403();

	// Validate fields
	CStringView name, begins, ends, full_results, ranking_expo;
	form_validate_not_blank(name, "name", "Round's name",
		CONTEST_ROUND_NAME_MAX_LEN);
	form_validate_not_blank(begins, "begins", "Begin time", is_safe_timestamp);
	form_validate(ends, "ends", "End time", is_safe_timestamp);
	form_validate(full_results, "full_results", "Full results time",
		is_safe_timestamp);
	form_validate(ranking_expo, "ranking_expo", "Show ranking since",
		is_safe_timestamp);

	if (form_validation_error)
		return api_error400(notifications);

	// Add round
	auto curr_date = mysql_date();
	auto stmt = mysql.prepare("INSERT contest_rounds(contest_id, name, item,"
			" begins, ends, full_results, ranking_exposure)"
		" SELECT ?, ?, COALESCE(MAX(item)+1, 0), ?, ?, ?, ?"
		" FROM contest_rounds WHERE contest_id=?");
	stmt.bind(0, contests_cid);
	stmt.bind(1, name);
	stmt.bind_copy(2, mysql_date(strtoull(begins)));

	if (ends.empty())
		stmt.bind(3, nullptr);
	else
		stmt.bind_copy(3, mysql_date(strtoull(ends)));

	if (full_results.empty())
		stmt.bind(4, nullptr);
	else
		stmt.bind_copy(4, mysql_date(strtoull(full_results)));

	if (ranking_expo.empty())
		stmt.bind(5, nullptr);
	else
		stmt.bind_copy(5, mysql_date(strtoull(ranking_expo)));

	stmt.bind(6, contests_cid);
	stmt.fixAndExecute();

	append(stmt.insert_id());
}

void Sim::api_contest_round_edit() {
	STACK_UNWINDING_MARK;

	using PERM = ContestPermissions;

	if (uint(~contests_perms & PERM::ADMIN))
		return api_error403();

	// Validate fields
	CStringView name, begins, ends, full_results, ranking_expo;
	form_validate_not_blank(name, "name", "Round's name",
		CONTEST_ROUND_NAME_MAX_LEN);
	form_validate_not_blank(begins, "begins", "Begin time", is_safe_timestamp);
	form_validate(ends, "ends", "End time", is_safe_timestamp);
	form_validate(full_results, "full_results", "Full results time",
		is_safe_timestamp);
	form_validate(ranking_expo, "ranking_expo", "Show ranking since",
		is_safe_timestamp);

	if (form_validation_error)
		return api_error400(notifications);

	// Add round
	auto curr_date = mysql_date();
	auto stmt = mysql.prepare("UPDATE contest_rounds"
		" SET name=?, begins=?, ends=?, full_results=?, ranking_exposure=?"
		" WHERE id=?");
	stmt.bind(0, name);
	stmt.bind_copy(1, mysql_date(strtoull(begins)));

	if (ends.empty())
		stmt.bind(2, nullptr);
	else
		stmt.bind_copy(2, mysql_date(strtoull(ends)));

	if (full_results.empty())
		stmt.bind(3, nullptr);
	else
		stmt.bind_copy(3, mysql_date(strtoull(full_results)));

	if (ranking_expo.empty())
		stmt.bind(4, nullptr);
	else
		stmt.bind_copy(4, mysql_date(strtoull(ranking_expo)));

	stmt.bind(5, contests_crid);
	stmt.fixAndExecute();
}

void Sim::api_contest_problem_add() {
	STACK_UNWINDING_MARK;

	using PERM = ContestPermissions;

	if (uint(~contests_perms & PERM::ADMIN))
		return api_error403();

	// Validate fields
	StringView name, problem_id;
	form_validate(name, "name", "Problem's name",
		CONTEST_PROBLEM_NAME_MAX_LEN);
	form_validate(problem_id, "problem_id", "Problem ID", isDigit,
		"Problem ID: invalid value");

	if (form_validation_error)
		return api_error400(notifications);

	auto stmt = mysql.prepare("SELECT owner, type, name FROM problems"
		" WHERE id=?");
	stmt.bindAndExecute(problem_id);

	// TODO: check problem permissions
	InplaceBuff<32> powner;
	std::underlying_type_t<ProblemType> ptype_u;
	InplaceBuff<PROBLEM_NAME_MAX_LEN> pname;
	stmt.res_bind_all(powner, ptype_u, pname);
	if (not stmt.next())
		return api_error404(concat("No problem was found with ID = ",
			problem_id));

	auto pperms = problems_get_permissions(powner, ProblemType(ptype_u));
	if (uint(~pperms & ProblemPermissions::VIEW))
		return api_error403("You have no permissions to use this problem");

	// Add problem
	stmt = mysql.prepare("INSERT contest_problems(contest_round_id, contest_id,"
			" problem_id, name, item, final_selecting_method)"
		" SELECT ?, ?, ?, ?, COALESCE(MAX(item)+1, 0), 0 FROM contest_problems"
		" WHERE contest_round_id=?");
	stmt.bindAndExecute(contests_crid, contests_cid, problem_id,
		(name.empty() ? pname : name), contests_crid);

	append(stmt.insert_id());
}

void Sim::api_contest_problem_statement(StringView problem_id) {
	STACK_UNWINDING_MARK;

	auto stmt = mysql.prepare("SELECT label, simfile FROM problems WHERE id=?");
	stmt.bindAndExecute(problem_id);

	InplaceBuff<PROBLEM_LABEL_MAX_LEN> label;
	InplaceBuff<1 << 16> simfile;
	stmt.res_bind_all(label, simfile);
	if (not stmt.next())
		return api_error404();

	return api_statement_impl(problem_id, label, simfile);
}

namespace {

struct SubmissionOwner {
	uint64_t id;
	InplaceBuff<32> name; // static size is set manually to save memory

	template<class... Args>
	SubmissionOwner(uint64_t sowner, Args&&... args) : id(sowner) {
		name.append(std::forward<Args>(args)...);
	}

	bool operator<(const SubmissionOwner& s) const noexcept {
		return id < s.id;
	}

	bool operator==(const SubmissionOwner& s) const noexcept {
		return id == s.id;
	}
};

} // anonymous namespace

void Sim::api_contest_ranking(StringView submissions_id_name,
	StringView query_id)
{
	STACK_UNWINDING_MARK;

	using PERM = ContestPermissions;

	if (uint(~contests_perms & PERM::VIEW))
		return api_error403();

	// Gather submissions owners
	auto stmt = mysql.prepare(concat("SELECT u.id, u.first_name, u.last_name"
		" FROM submissions s JOIN users u ON s.owner=u.id"
		" WHERE s.", submissions_id_name, "=? AND s.type=" STYPE_FINAL_STR
		" GROUP BY (u.id) ORDER BY u.id"));
	stmt.bindAndExecute(query_id);

	uint64_t id;
	InplaceBuff<USER_FIRST_NAME_MAX_LEN> fname;
	InplaceBuff<USER_LAST_NAME_MAX_LEN> lname;
	stmt.res_bind_all(id, fname, lname);

	std::vector<SubmissionOwner> sowners;
	while (stmt.next())
		sowners.emplace_back(id, fname, ' ' , lname);

	// Gather submissions
	bool first_owner = true;
	uint64_t owner, prev_owner;
	uint64_t crid, cpid;
	std::underlying_type_t<SubmissionStatus> status;
	int64_t score;
	decltype(mysql_date()) curr_date;

	if (uint(contests_perms & PERM::ADMIN)) {
		stmt = mysql.prepare(concat("SELECT id, owner, contest_round_id,"
				" contest_problem_id, status, score FROM submissions "
			" WHERE ", submissions_id_name, "=? AND type=" STYPE_FINAL_STR
			" ORDER BY owner"));
		stmt.bindAndExecute(query_id);
		stmt.res_bind_all(id, owner, crid, cpid, status, score);

	} else {
		stmt = mysql.prepare(concat("SELECT s.id, s.owner, s.contest_round_id,"
				" s.contest_problem_id, s.status, s.score"
			" FROM submissions s JOIN contest_rounds cr ON"
				" cr.id=s.contest_round_id AND cr.begins<=? AND"
				" (cr.full_results IS NULL OR cr.full_results<=?) AND"
				" (cr.ranking_exposure IS NOT NULL AND cr.ranking_exposure<=?)"
			" WHERE s.", submissions_id_name, "=? AND s.type=" STYPE_FINAL_STR
			" ORDER BY s.owner"));
		curr_date = mysql_date();
		stmt.bindAndExecute(curr_date, curr_date, curr_date, query_id);
		stmt.res_bind_all(id, owner, crid, cpid, status, score);
	}

	append('[');
	const uint64_t session_uid = (session_is_open ? strtoull(session_user_id)
		: 0);
	bool show_id = false;
	while (stmt.next()) {
		// Owner changes
		if (first_owner or owner != prev_owner) {
			auto it = binaryFind(sowners, SubmissionOwner(owner));
			if (it == sowners.end())
				continue; // Ignore submission as there is no owner to bind it
				          // to (this maybe a little race condition, but if the
				          // user will query again it will surely not be the
				          // case again (with the current owner))

			if (first_owner) {
				append('[');
				first_owner = false;
			} else {
				--resp.content.size; // remove trailing ','
				append("\n]],[");
			}

			prev_owner = owner;
			show_id = (uint(contests_perms & PERM::ADMIN) or
				(session_is_open and session_uid == owner));
			// Owner
			if (show_id)
				append(owner);
			else
				append("null");
			// Owner name
			append(',', jsonStringify(it->name), ",[");
		}

		append("\n[");
		if (show_id)
			append(id, ',');
		else
			append("null,");

		append(crid, ',',
			cpid, ',');

		append_submission_status(SubmissionStatus(status), true);
		// TODO: make score revealing work with the ranking
		append(',', score, "],");
	}

	if (first_owner) // no submission was appended
		append(']');
	else {
		--resp.content.size; // remove trailing ','
		append("\n]]]");
	}
}
