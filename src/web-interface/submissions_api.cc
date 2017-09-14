#include "sim.h"

#include <sim/jobs.h>
#include <sim/submission.h>
#include <simlib/filesystem.h>
#include <simlib/process.h>

using std::string;

void Sim::api_submissions() {
	STACK_UNWINDING_MARK;
	using PERM = SubmissionPermissions;
	using CUM = ContestUserMode;

	if (not session_open())
		return api_error403();

	InplaceBuff<512> qfields, qwhere;
	qfields.append("SELECT s.id, s.type, s.owner, c.owner, cu.mode, p.owner,"
		" u.username, s.problem_id, p.name, s.round_id, r.name,"
		" s.parent_round_id, pr.name, pr.reveal_score, pr.full_results,"
		" s.contest_round_id, c.name, s.submit_time, s.status, s.score");
	qwhere.append(" FROM submissions s"
		" LEFT JOIN users u ON u.id=s.owner"
		" STRAIGHT_JOIN problems p ON p.id=s.problem_id"
		" LEFT JOIN rounds r ON r.id=s.round_id"
		" LEFT JOIN rounds pr ON pr.id=s.parent_round_id"
		" LEFT JOIN rounds c ON c.id=s.contest_round_id"
		" LEFT JOIN contests_users cu ON cu.contest_id=s.contest_round_id AND"
			" cu.user_id=", session_user_id,
		" WHERE s.type!=" STYPE_VOID_STR);

	bool allow_access = (session_user_type == UserType::ADMIN);
	bool select_one = false;

	// Process restrictions
	StringView next_arg = url_args.extractNextArg();
	for (uint mask = 0; next_arg.size(); next_arg = url_args.extractNextArg()) {
		constexpr uint ID_COND = 1;
		constexpr uint STYPE_COND = 2;
		constexpr uint ROUND_OR_PROBLEM_ID_COND = 4;
		constexpr uint USER_ID_COND = 8;

		auto arg = decodeURI(next_arg);
		char cond = arg[0];
		StringView arg_id = StringView{arg}.substr(1);

		// submission type
		if (cond == 't' and ~mask & STYPE_COND) {
			if (arg_id == "N")
				qwhere.append(" AND s.type=" STYPE_NORMAL_STR);
			else if (arg_id == "F")
				qwhere.append(" AND s.type=" STYPE_FINAL_STR);
			else if (arg_id == "I")
				qwhere.append(" AND s.type=" STYPE_IGNORED_STR);
			else if (arg_id == "S")
				qwhere.append(" AND s.type=" STYPE_PROBLEM_SOLUTION_STR);
			else
				return api_error400(concat("Invalid submission type: ",
					arg_id));

			mask |= STYPE_COND;
			continue;
		}

		if (not isDigit(arg_id))
			return api_error400();

		// conditional
		if (isIn(cond, "<>") and ~mask & ID_COND) {
			qwhere.append(" AND s.id", arg);
			mask |= ID_COND;

		} else if (cond == '=' and ~mask & ID_COND) {
			allow_access = true; // Permissions will be checked on fetching data

			qfields.append(", u.first_name, u.last_name,"
				" s.initial_report, s.final_report");
			select_one = true;

			qwhere.append(" AND s.id", arg);
			mask |= ID_COND;

		// problem's or round's id
		} else if (isIn(cond, "pCRP") and ~mask & ROUND_OR_PROBLEM_ID_COND) {
			if (cond == 'p') {
				qwhere.append(" AND s.problem_id=", arg_id);
				if (not allow_access) {
					InplaceBuff<32> p_owner;
					auto stmt = mysql.prepare("SELECT owner FROM problems"
						" WHERE id=?");
					stmt.bindAndExecute(arg_id);
					stmt.res_bind_all(p_owner);
					if (stmt.next() and p_owner == session_user_id)
						allow_access = true;
				}

			} else {
				if (not allow_access) {
					StringView query;
					switch (cond) {
					case 'C': query = "SELECT c.owner, cu.mode FROM rounds c"
						" LEFT JOIN contests_users cu ON cu.contest_id=c.id"
							" AND cu.user_id=?"
						" WHERE c.id=?";
						break;

					case 'R': query = "SELECT c.owner, cu.mode FROM rounds r"
						" STRAIGHT_JOIN rounds c ON c.id=r.parent"
						" LEFT JOIN contests_users cu ON cu.contest_id=c.id"
							" AND cu.user_id=?"
						" WHERE r.id=?";
						break;

					case 'P': query = "SELECT c.owner, cu.mode FROM rounds r"
						" STRAIGHT_JOIN rounds c ON c.id=r.grandparent"
						" LEFT JOIN contests_users cu ON cu.contest_id=c.id"
							" AND cu.user_id=?"
						" WHERE r.id=?";
						break;
					}
					auto stmt = mysql.prepare(query);
					stmt.bindAndExecute(session_user_id, arg_id);

					InplaceBuff<32> c_owner;
					uint cu_mode;
					stmt.res_bind_all(c_owner, cu_mode);
					if (not stmt.next())
						return api_error404(concat("Invalid query condition ",
							cond, " - no such round was found"));

					if (c_owner == session_user_id or
						(not stmt.is_null(1) and
							cu_mode == (uint)CUM::MODERATOR))
					{
						allow_access = true;
					}
				}

				if (cond == 'C')
					qwhere.append(" AND s.contest_round_id=", arg_id);
				else if (cond == 'R')
					qwhere.append(" AND s.parent_round_id=", arg_id);
				else if (cond == 'P')
					qwhere.append(" AND s.round_id=", arg_id);
			}

			mask |= ROUND_OR_PROBLEM_ID_COND;

		// user (creator)
		} else if (cond == 'u' and ~mask & USER_ID_COND) {
			qwhere.append(" AND s.owner=", arg_id);
			if (arg_id == session_user_id)
				allow_access = true;

			mask |= USER_ID_COND;

		} else
			return api_error400();

	}

	if (not allow_access)
		return api_error403();

	// Execute query
	qfields.append(qwhere, " ORDER BY s.id DESC LIMIT 50");
	auto res = mysql.query(qfields);

	resp.headers["content-type"] = "text/plain; charset=utf-8";
	append("[");

	while (res.next()) {
		StringView sid = res[0];
		SubmissionType stype = SubmissionType(strtoull(res[1]));
		StringView sowner = res[2];
		StringView c_owner = (res.is_null(3) ? "" : res[3]);
		StringView cu_mode = res[4];
		StringView p_owner = res[5];

		SubmissionPermissions perms = submissions_get_permissions(sowner, stype,
			c_owner, (res.is_null(4) ? CUM::IS_NULL : CUM(strtoull(cu_mode))),
			p_owner);

		if (perms == PERM::NONE)
			return api_error403();

		StringView sowner_username = res[6];
		StringView p_id = res[7];
		StringView p_name = res[8];
		StringView r_id = res[9];
		StringView r_name = res[10];
		StringView pr_id = res[11];
		StringView pr_name = res[12];
		bool reveal_score = (res[13] != "0");
		bool show_full_results = (bool(uint(perms & PERM::VIEW_FULL_REPORT)) or
			res.is_null(14) or res[14] <= date());
		StringView c_id = res[15];
		StringView c_name = res[16];
		StringView submit_time = res[17];
		SubmissionStatus status = SubmissionStatus(strtoull(res[18]));
		StringView score = res[19];

		append("\n[", sid, ',');

		// Type
		switch (stype) {
		case SubmissionType::NORMAL: append("\"Normal\","); break;
		case SubmissionType::FINAL: append("\"Final\","); break;
		case SubmissionType::VOID: throw_assert(false); break;
		case SubmissionType::IGNORED: append("\"Ignored\","); break;
		case SubmissionType::PROBLEM_SOLUTION: append("\"Problem solution\",");
			break;
		}

		// Onwer's id
		if (res.is_null(2))
			append("null,");
		else
			append(sowner, ',');

		// Onwer's username
		if (res.is_null(6))
			append("null,");
		else
			append("\"", sowner_username, "\",");

		// Problem
		if (res.is_null(7))
			append("null,null,");
		else
			append(p_id, ',', jsonStringify(p_name), ',');

		// Rounds
		if (res.is_null(9))
			append("null,null,null,null,null,null,");
		else {
			append(r_id, ',',
				jsonStringify(r_name), ',',
				pr_id, ',',
				jsonStringify(pr_name), ',',
				c_id, ',',
				jsonStringify(c_name), ',');
		}

		// Submit time
		append("\"", submit_time, "\",");

		/* Status: (CSS class, text) */
		using SS = SubmissionStatus;

		if (is_special(status)) {
			if (status == SS::PENDING)
				append("[\"\",\"Pending\"],");
			else if (status == SS::COMPILATION_ERROR)
				append("[\" purple\",\"Compilation failed\"],");
			else if (status == SS::CHECKER_COMPILATION_ERROR)
				append("[\" blue\",\"Checker compilation failed\"],");
			else if (status == SS::JUDGE_ERROR)
				append("[\" blue\",\"Judge error\"],");
			else
				append("[\"\",\"Unknown\"],");

		// Final
		} else if (show_full_results) {
			if ((status & SS::FINAL_MASK) == SS::OK)
				append("[\"green\",\"OK\"],");
			else if ((status & SS::FINAL_MASK) == SS::WA)
				append("[\"red\",\"Wrong answer\"],");
			else if ((status & SS::FINAL_MASK) == SS::TLE)
				append("[\"yellow\",\"Time limit exceeded\"],");
			else if ((status & SS::FINAL_MASK) == SS::MLE)
				append("[\"yellow\",\"Memory limit exceeded\"],");
			else if ((status & SS::FINAL_MASK) == SS::RTE)
				append("[\"intense-red\",\"Runtime error\"],");
			else
				append("[\"\",\"Unknown\"],");

		// Initial
		} else {
			if ((status & SS::INITIAL_MASK) == SS::INITIAL_OK)
				append("[\"initial green\",\"OK\"],");
			else if ((status & SS::INITIAL_MASK) == SS::INITIAL_WA)
				append("[\"initial red\",\"Wrong answer\"],");
			else if ((status & SS::INITIAL_MASK) == SS::INITIAL_TLE)
				append("[\"initial yellow\",\"Time limit exceeded\"],");
			else if ((status & SS::INITIAL_MASK) == SS::INITIAL_MLE)
				append("[\"initial yellow\",\"Memory limit exceeded\"],")
					;
			else if ((status & SS::INITIAL_MASK) == SS::INITIAL_RTE)
				append("[\"initial intense-red\",\"Runtime error\"],");
			else
				append("[\"\",\"Unknown\"],");
		}

		// Score
		if (not res.is_null(19) and (show_full_results or reveal_score))
			append(score, ',');
		else
			append("null,");

		// Actions
		append('"');

		if (uint(perms & PERM::VIEW))
			append('v');
		if (uint(perms & PERM::VIEW_SOURCE))
			append('s');
		if (uint(perms & PERM::VIEW_RELATED_JOBS))
			append('j');
		if (uint(perms & PERM::CHANGE_TYPE))
			append('C');
		if (uint(perms & PERM::REJUDGE))
			append('R');
		if (uint(perms & PERM::DELETE))
			append('D');

		append("\"");

		// Append
		if (select_one) {
			// User first and last name
			if (res.is_null(20))
				append(",null,null");
			else
				append(',', jsonStringify(res[20]), ',',
					jsonStringify(res[21]));

			// Reports
			append(',', jsonStringify(res[22]));
			if (show_full_results)
				append(',', jsonStringify(res[23]));
			else
				append(",null");
		}

		append("],");
	}

	if (resp.content.back() == ',')
		--resp.content.size;

	append("\n]");
}

void Sim::api_submission() {
	STACK_UNWINDING_MARK;
	using CUM = ContestUserMode;

	if (not session_open())
		return api_error403();

	StringView next_arg = url_args.extractNextArg();
	if (next_arg == "add")
		return api_submission_add();
	else if (not isDigit(next_arg))
		return api_error400();

	submissions_sid = next_arg;

	InplaceBuff<32> sowner, c_owner, p_owner;
	uint stype, cu_mode;
	auto stmt = mysql.prepare("SELECT s.owner, s.type, c.owner, cu.mode,"
			" p.owner"
		" FROM submissions s"
		" LEFT JOIN rounds c ON c.id=s.contest_round_id"
		" STRAIGHT_JOIN problems p ON p.id=s.problem_id"
		" LEFT JOIN contests_users cu ON cu.contest_id=s.contest_round_id"
			" AND cu.user_id=?"
		" WHERE s.id=?");
	stmt.bindAndExecute(session_user_id, submissions_sid);
	stmt.res_bind_all(sowner, stype, c_owner, cu_mode, p_owner);
	if (not stmt.next())
		return api_error404();

	submissions_perms = submissions_get_permissions(sowner,
		SubmissionType(stype), (stmt.is_null(1) ? StringView{} : c_owner),
		(stmt.is_null(2) ? CUM::IS_NULL : CUM(cu_mode)), p_owner);

	// Subpages
	next_arg = url_args.extractNextArg();
	if (next_arg == "source")
		return api_submission_source();
	if (next_arg == "download")
		return api_submission_download();

	if (request.method != server::HttpRequest::POST)
		return api_error400();

	// Subpages causing action
	if (next_arg == "rejudge")
		return api_submission_rejudge();
	if (next_arg == "chtype")
		return api_submission_change_type();
	if (next_arg == "delete")
		return api_submission_delete();

	return api_error404();
}

void Sim::api_submission_add() {
	STACK_UNWINDING_MARK;

	StringView next_arg = url_args.extractNextArg();
	// Load problem id and contest problem id (if specified)
	StringView problem_id, contest_problem_id;
	for (; next_arg.size(); next_arg = url_args.extractNextArg()) {
		if (next_arg[0] == 'p' and isDigit(next_arg.substr(1)) and
			problem_id.empty())
		{
			problem_id = next_arg.substr(1);
		} else if (hasPrefix(next_arg, "cp") and isDigit(next_arg.substr(2)) and
			contest_problem_id.empty())
		{
			contest_problem_id = next_arg.substr(2);
		} else
			return api_error400();
	}

	if (problem_id.empty())
		return api_error400("problem have to be specified");

	// Check permissions to the problem
	auto stmt = mysql.prepare("SELECT owner, type FROM problems WHERE id=?");
	InplaceBuff<32> powner;
	uint ptype;
	stmt.bindAndExecute(problem_id);
	stmt.res_bind_all(powner, ptype);
	if (not stmt.next())
		return api_error404();

	ProblemPermissions pperms = problems_get_permissions(powner,
		ProblemType(ptype));
	if (uint(~pperms & ProblemPermissions::SUBMIT))
		return api_error403();

	// TODO: constest_problem_id

	// Validate fields
	CStringView solution_tmp_path = request.form_data.file_path("solution");
	StringView code = request.form_data.get("code");
	bool ignored_submission = (uint(pperms & ProblemPermissions::SUBMIT_IGNORED)
		and request.form_data.exist("ignored"));

	if ((code.empty() ^ request.form_data.get("solution").empty()) == 0) {
		add_notification("error", "You have to either choose a file or paste"
			" the code");
		return api_error400(notifications);
	}

	// Check the solution size
	// File
	if (code.empty()) {
		struct stat sb;
		if (stat(solution_tmp_path.c_str(), &sb))
			THROW("stat() failed", error());

		// Check if solution is too big
		if ((uint64_t)sb.st_size > SOLUTION_MAX_SIZE) {
			add_notification("error", "Solution is too big (maximum allowed "
				"size: ", SOLUTION_MAX_SIZE, " bytes = ",
				SOLUTION_MAX_SIZE >> 10, " KB)");
			return api_error400(notifications);
		}
	// Code
	} else if (code.size() > SOLUTION_MAX_SIZE) {
		add_notification("error", "Solution is too big (maximum allowed "
			"size: ", SOLUTION_MAX_SIZE, " bytes = ",
			SOLUTION_MAX_SIZE >> 10, " KB)");
		return api_error400(notifications);
	}

	// Insert submission
	stmt = mysql.prepare("INSERT submissions (owner, problem_id, round_id,"
			" parent_round_id, contest_round_id, type, status, submit_time,"
			" last_judgment, initial_report, final_report)"
		" VALUES(?, ?, NULL, NULL, NULL, "
			STYPE_VOID_STR ", " SSTATUS_PENDING_STR ", ?, ?, '', '')");
	stmt.bindAndExecute(session_user_id, problem_id, date(),
		date("%Y-%m-%d %H:%M:%S", 0));
	auto submission_id = stmt.insert_id();

	// Fill the submission's source file
	// File
	auto solution_dest = concat<64>("solutions/", submission_id, ".cpp");
	if (code.empty()) {
		if (rename(solution_tmp_path, solution_dest.to_cstr()))
			THROW("rename() failed", error());
	// Code
	} else
		putFileContents(solution_dest.to_cstr(), code);

	// Create a job to judge the submission
	stmt = mysql.prepare("INSERT jobs (creator, status, priority, type, added,"
			" aux_id, info, data)"
		" VALUES(?, " JSTATUS_PENDING_STR ", ?, ?, ?, ?, ?, '')");
	stmt.bindAndExecute(session_user_id, priority(JobType::JUDGE_SUBMISSION),
		(uint)JobType::JUDGE_SUBMISSION, date(), submission_id,
		jobs::dumpString(problem_id));

	// Activate the submission
	stmt = mysql.prepare("UPDATE submissions SET type=? WHERE id=?");
	stmt.bindAndExecute(
		std::underlying_type_t<SubmissionType>(ignored_submission ?
			SubmissionType::IGNORED : SubmissionType::NORMAL),
		submission_id);

	jobs::notify_job_server();
	append(submission_id);
}

void Sim::api_submission_source() {
	STACK_UNWINDING_MARK;

	if (uint(~submissions_perms & SubmissionPermissions::VIEW_SOURCE))
		return api_error403();

	append(cpp_syntax_highlighter(getFileContents(
		concat("solutions/", submissions_sid, ".cpp").to_cstr())));
}

void Sim::api_submission_download() {
	STACK_UNWINDING_MARK;

	if (uint(~submissions_perms & SubmissionPermissions::VIEW_SOURCE))
		return api_error403();


	resp.headers["Content-type"] = "text/x-c++src";
	resp.headers["Content-Disposition"] = concat_tostr("attachment; filename=",
		submissions_sid, ".cpp");

	resp.content = concat("solutions/", submissions_sid, ".cpp");
	resp.content_type = server::HttpResponse::FILE;
}

void Sim::api_submission_rejudge() {
	STACK_UNWINDING_MARK;

	if (uint(~submissions_perms & SubmissionPermissions::REJUDGE))
		return api_error403();

	InplaceBuff<32> problem_id;
	auto stmt = mysql.prepare("SELECT problem_id FROM submissions WHERE id=?");
	stmt.bindAndExecute(submissions_sid);
	stmt.res_bind_all(problem_id);
	throw_assert(stmt.next());

	stmt = mysql.prepare("INSERT jobs (creator, status, priority, type, added,"
			" aux_id, info, data)"
		" VALUES(?, " JSTATUS_PENDING_STR ", ?, ?, ?, ?, ?, '')");
	stmt.bindAndExecute(session_user_id, priority(JobType::JUDGE_SUBMISSION),
		(uint)JobType::JUDGE_SUBMISSION, date(), submissions_sid,
		jobs::dumpString(problem_id));

	jobs::notify_job_server();
}

void Sim::api_submission_change_type() {
	STACK_UNWINDING_MARK;
	using SS = SubmissionStatus;
	using ST = SubmissionType;

	if (uint(~submissions_perms & SubmissionPermissions::CHANGE_TYPE))
		return api_error403();

	StringView type_s = request.form_data.get("type");

	SubmissionType new_type;
	if (type_s == "I")
		new_type = ST::IGNORED;
	else if (type_s == "N")
		new_type = ST::NORMAL;
	else
		return api_error400("Invalid type, it must be one of those: I or N");

	// Lock the table to be able to safely modify the submission
	mysql.update("LOCK TABLES submissions WRITE");
	auto lock_guard = make_call_in_destructor([&]{
		mysql.update("UNLOCK TABLES");
	});

	auto res = mysql.query(concat("SELECT status, owner, round_id"
		" FROM submissions WHERE id=", submissions_sid));
	throw_assert(res.next());

	SS status = SS(strtoull(res[0]));
	StringView owner = (res.is_null(1) ? "" : res[1]);
	StringView round_id = (res.is_null(2) ? "" : res[2]);

	// Cannot be FINAL
	if (is_special(status)) {
		auto stmt = mysql.prepare("UPDATE submissions SET type=? WHERE id=?");
		stmt.bindAndExecute(uint(new_type), submissions_sid);
		return;
	}

	// May be of type FINAL
	SignalBlocker sb; // This part shouldn't be interrupted

	// Update the submission first
	auto stmt = mysql.prepare("UPDATE submissions SET type=?, final_candidate=?"
		" WHERE id=?");
	stmt.bindAndExecute(uint(new_type), (new_type == ST::NORMAL),
		submissions_sid);

	submission::update_final(mysql, owner, round_id, false);
}

void Sim::api_submission_delete() {
	STACK_UNWINDING_MARK;

	if (uint(~submissions_perms & SubmissionPermissions::DELETE))
		return api_error403();

	// Lock the table to be able to safely delete the submission
	mysql.update("LOCK TABLES submissions WRITE");
	auto lock_guard = make_call_in_destructor([&]{
		mysql.update("UNLOCK TABLES");
	});

	auto res = mysql.query(concat("SELECT owner, round_id FROM submissions"
		" WHERE id=", submissions_sid));
	throw_assert(res.next());

	SignalBlocker sb; // This part shouldn't be interrupted

	mysql.update(concat("DELETE FROM submissions WHERE id=", submissions_sid));

	submission::update_final(mysql, (res.is_null(0) ? "" : res[0]),
		(res.is_null(1) ? "" : res[1]), false);
}
