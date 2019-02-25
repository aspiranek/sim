#include "main.h"
#include "problem_add_or_reupload_job_handler_base.h"

#include <simlib/libzip.h>
#include <simlib/sim/problem_package.h>

void ProblemAddOrReuploadJobHandlerBase::load_job_log_from_DB() {
	STACK_UNWINDING_MARK;
	auto stmt = mysql.prepare("SELECT data FROM jobs WHERE id=?");
	stmt.bindAndExecute(job_id);
	stmt.res_bind_all(job_log_holder);
	stmt.next();
}

void ProblemAddOrReuploadJobHandlerBase::assert_transaction_is_open() {
	STACK_UNWINDING_MARK;

	bool in_transaction;
	auto stmt = mysql.prepare("SELECT @@in_transaction");
	stmt.bindAndExecute();
	stmt.res_bind_all(in_transaction);
	throw_assert(stmt.next() and in_transaction);
}

void ProblemAddOrReuploadJobHandlerBase::build_package() {
	STACK_UNWINDING_MARK;
	if (failed())
		return;

	assert_transaction_is_open();

	replace_db_job_log = true;

	auto source_package = internal_file_path(job_file_id);

	mysql.update("INSERT INTO internal_files VALUES()");
	tmp_file_id = mysql.insert_id();

	/* Construct Simfile */

	sim::Conver conver;
	conver.package_path(source_package.to_string());

	// Set Conver options
	sim::Conver::Options copts;
	copts.name = info.name;
	copts.label = info.label;
	copts.memory_limit = info.memory_limit;
	copts.global_time_limit = info.global_time_limit;
	copts.max_time_limit = MAX_TIME_LIMIT;
	copts.reset_time_limits_using_model_solution = info.reset_time_limits;
	copts.ignore_simfile = info.ignore_simfile;
	copts.seek_for_new_tests = info.seek_for_new_tests;
	copts.reset_scoring = info.reset_scoring;
	copts.require_statement = true;
	copts.rtl_opts.min_time_limit = MIN_TIME_LIMIT;
	copts.rtl_opts.solution_runtime_coefficient = SOLUTION_RUNTIME_COEFFICIENT;

	sim::Conver::ConstructionResult cr;
	try {
		cr = conver.construct_simfile(copts);
	} catch (const std::exception& e) {
		return set_failure(conver.report(), "Conver failed: ", e.what());
	}

	// Check problem's name's length
	if (cr.simfile.name.size() > PROBLEM_NAME_MAX_LEN) {
		return set_failure("Problem's name is too long (max allowed length: ",
			PROBLEM_NAME_MAX_LEN, ')');
	}

	// Check problem's label's length
	if (cr.simfile.label.size() > PROBLEM_LABEL_MAX_LEN) {
		return set_failure("Problem's label is too long (max allowed length: ",
			PROBLEM_LABEL_MAX_LEN, ')');
	}

	job_log(conver.report());

	/* Create the temporary package */

	// Update job record
	mysql.prepare("UPDATE jobs SET tmp_file_id=? WHERE id=?")
		.bindAndExecute(tmp_file_id.value(), job_id);
	auto tmp_package = internal_file_path(tmp_file_id.value());
	// Copy source_package to tmp_package, substituting Simfile in the fly
	{
		ZipFile src_zip(source_package, ZIP_RDONLY);
		simfile_str = cr.simfile.dump();
		auto simfile_path = concat(cr.pkg_master_dir, "Simfile");

		package_file_remover.reset(tmp_package);
		ZipFile dest_zip(tmp_package, ZIP_CREATE | ZIP_TRUNCATE);

		auto eno = src_zip.entries_no();
		for (decltype(eno) i = 0; i < eno; ++i) {
			auto entry_name = src_zip.get_name(i);
			if (entry_name == simfile_path) {
				dest_zip.file_add(simfile_path,
					dest_zip.source_buffer(simfile_str));
			} else {
				dest_zip.file_add(entry_name, dest_zip.source_zip(src_zip, i));
			}
		}

		dest_zip.close(); // Write all data to the dest_zip
	}

	switch (cr.status) {
	case sim::Conver::Status::COMPLETE:
		need_model_solution_judge_report = false;
		return;
	case sim::Conver::Status::NEED_MODEL_SOLUTION_JUDGE_REPORT:
		need_model_solution_judge_report = true;
		return;
	}
}

void ProblemAddOrReuploadJobHandlerBase::job_done(bool& job_was_canceled) {
	STACK_UNWINDING_MARK;
	if (failed())
		return;

	EnumVal<JobStatus> status = JobStatus::DONE;
	EnumVal<JobType> type = job_type;
	if (need_model_solution_judge_report) {
		status = JobStatus::PENDING;
		switch (job_type) {
		case JobType::ADD_PROBLEM:
			type = JobType::ADD_JUDGE_MODEL_SOLUTION;
			break;

		case JobType::REUPLOAD_PROBLEM:
			type = JobType::REUPLOAD_JUDGE_MODEL_SOLUTION;
			break;

		default:
			THROW("Unexpected job type");
		}

	} else {
		switch (job_type) {
		case JobType::ADD_PROBLEM:
		case JobType::REUPLOAD_PROBLEM:
			break;

		case JobType::ADD_JUDGE_MODEL_SOLUTION:
			status = JobStatus::PENDING;
			type = JobType::ADD_PROBLEM;
			break;

		case JobType::REUPLOAD_JUDGE_MODEL_SOLUTION:
			status = JobStatus::PENDING;
			type = JobType::REUPLOAD_PROBLEM;
			break;

		default:
			THROW("Unexpected job type");
		}
	}

	auto stmt = mysql.prepare("UPDATE jobs"
		" SET tmp_file_id=?, type=?, priority=?, status=?, aux_id=?, info=?,"
			" data=?"
		" WHERE id=? AND status!=" JSTATUS_CANCELED_STR);
	stmt.bindAndExecute(tmp_file_id, type, priority(type), status, problem_id,
		info.dump(), get_log(), job_id);
	job_was_canceled = (stmt.affected_rows() == 0);
}

static SubmissionLanguage filename_to_lang(StringView extension) {
	STACK_UNWINDING_MARK;

	auto lang = sim::filename_to_lang(extension);
	switch (lang) {
	case sim::SolutionLanguage::C11: return SubmissionLanguage::C11;
	case sim::SolutionLanguage::CPP11: return SubmissionLanguage::CPP11;
	case sim::SolutionLanguage::CPP14: return SubmissionLanguage::CPP14;
	case sim::SolutionLanguage::PASCAL: return SubmissionLanguage::PASCAL;
	case sim::SolutionLanguage::UNKNOWN: THROW("Not supported language");
	}

	throw_assert(false);
}

void ProblemAddOrReuploadJobHandlerBase::open_package() {
	STACK_UNWINDING_MARK;
	if (failed())
		return;

	assert_transaction_is_open();

	zip = ZipFile(internal_file_path(tmp_file_id.value()), ZIP_RDONLY);
	master_dir = sim::zip_package_master_dir(zip);
	simfile_str = zip.extract_to_str(zip.get_index(concat(master_dir, "Simfile")));

	simfile = sim::Simfile(simfile_str);
	simfile.load_name();
	simfile.load_label();
	simfile.load_solutions();

	current_date = mysql_date();
}

void ProblemAddOrReuploadJobHandlerBase::add_problem_to_DB() {
	STACK_UNWINDING_MARK;
	if (failed())
		return;

	open_package();

	auto stmt = mysql.prepare("INSERT INTO problems(file_id, type, name, label,"
			" simfile, owner, added, last_edit)"
		" VALUES(?,?,?,?,?,?,?,?)");
	stmt.bindAndExecute(tmp_file_id.value(),
		EnumVal<ProblemType>(info.problem_type), simfile.name, simfile.label,
		simfile_str, job_creator, current_date, current_date);

	tmp_file_id = std::nullopt;
	problem_id = stmt.insert_id();
}

void ProblemAddOrReuploadJobHandlerBase::replace_problem_in_DB() {
	STACK_UNWINDING_MARK;
	if (failed())
		return;

	open_package();

	// Add job to delete old problem file
	mysql.prepare("INSERT INTO jobs(file_id, creator, type, priority, status,"
			" added, aux_id, info, data)"
		" SELECT file_id, NULL, " JTYPE_DELETE_FILE_STR ", ?, "
			JSTATUS_PENDING_STR ", ?, NULL, '', '' FROM problems WHERE id=?")
		.bindAndExecute(priority(JobType::DELETE_FILE), current_date,
			problem_id.value());

	// Update problem
	auto stmt = mysql.prepare("UPDATE problems"
		" SET file_id=?, type=?, name=?, label=?, simfile=?, last_edit=?"
		" WHERE id=?");
	stmt.bindAndExecute(tmp_file_id.value(),
		EnumVal<ProblemType>(info.problem_type), simfile.name, simfile.label,
		simfile_str, current_date, problem_id.value());

	tmp_file_id = std::nullopt;

	// Schedule jobs to delete old solutions files
	mysql.prepare("INSERT INTO jobs(file_id, creator, type, priority, status,"
			" added, aux_id, info, data)"
		" SELECT file_id, NULL, " JTYPE_DELETE_FILE_STR ", ?, "
			JSTATUS_PENDING_STR ", ?, NULL, '', ''"
		" FROM submissions"
		" WHERE problem_id=? AND type=" STYPE_PROBLEM_SOLUTION_STR)
		.bindAndExecute(priority(JobType::DELETE_FILE), current_date, problem_id);

	// Delete old solution submissions
	mysql.prepare("DELETE FROM submissions"
		" WHERE problem_id=? AND type=" STYPE_PROBLEM_SOLUTION_STR)
		.bindAndExecute(problem_id);
}

void ProblemAddOrReuploadJobHandlerBase::submit_solutions() {
	STACK_UNWINDING_MARK;
	if (failed())
		return;

	assert_transaction_is_open();

	job_log("Submitting solutions...");
	const auto zero_date = mysql_date(0);
	EnumVal<SubmissionLanguage> lang;
	auto submission_inserter = mysql.prepare("INSERT INTO submissions (file_id,"
			" owner, problem_id, contest_problem_id, contest_round_id,"
			" contest_id, type, language, initial_status, full_status,"
			" submit_time, last_judgment, initial_report, final_report)"
		" VALUES(?, NULL, ?, NULL, NULL, NULL, " STYPE_PROBLEM_SOLUTION_STR
			", ?, " SSTATUS_PENDING_STR ", " SSTATUS_PENDING_STR
			", ?, ?, '', '')");
	uint64_t file_id;
	submission_inserter.bind_all(file_id, problem_id, lang, current_date,
		zero_date);

	auto file_inserter = mysql.prepare("INSERT INTO internal_files VALUES()");

	for (auto const& solution : simfile.solutions) {
		job_log("Submit: ", solution);

		file_inserter.execute();
		file_id = file_inserter.insert_id();
		lang = filename_to_lang(solution);
		submission_inserter.execute();

		// Save the submission source code
		zip.extract_to_file(zip.get_index(concat(master_dir, solution)),
			internal_file_path(file_id));
	}

	// Add jobs to judge the solutions
	mysql.prepare("INSERT INTO jobs(creator, type, priority, status, added,"
			" aux_id, info, data)"
		" SELECT NULL, " JTYPE_JUDGE_SUBMISSION_STR ", ?, " JSTATUS_PENDING_STR
			", ?, id, ?, ''"
		" FROM submissions"
		" WHERE problem_id=? AND type=" STYPE_PROBLEM_SOLUTION_STR
		" ORDER BY id")
		// Problem's solutions are more important than the ordinary submissions
		.bindAndExecute(priority(JobType::JUDGE_SUBMISSION) + 1, current_date,
			jobs::dumpString(intentionalUnsafeStringView(toStr(problem_id.value()))),
			problem_id.value());

	job_log("Done.");
}
