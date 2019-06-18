#include "delete_problem_job_handler.h"
#include "main.h"

#include <sim/constants.h>

void DeleteProblemJobHandler::run() {
	STACK_UNWINDING_MARK;
	auto transaction = mysql.start_transaction();

	// Check whether the problem is not used as a contest problem
	{
		auto stmt = mysql.prepare("SELECT 1 FROM contest_problems"
		                          " WHERE problem_id=? LIMIT 1");
		stmt.bindAndExecute(problem_id);
		if (stmt.next()) {
			return set_failure(
			   "There exists a contest problem that uses (attaches) this "
			   "problem. You have to delete all of them to be able to delete "
			   "this problem.");
		}
	}

	// Assure that problem exist and log its Simfile
	{
		auto stmt = mysql.prepare("SELECT simfile FROM problems WHERE id=?");
		stmt.bindAndExecute(problem_id);
		InplaceBuff<1> simfile;
		stmt.res_bind_all(simfile);
		if (not stmt.next())
			return set_failure("Problem does not exist");

		job_log("Deleted problem Simfile:\n", simfile);
	}

	// Add job to delete problem file
	mysql
	   .prepare("INSERT INTO jobs(file_id, creator, type, priority, status,"
	            " added, aux_id, info, data) "
	            "SELECT file_id, NULL, " JTYPE_DELETE_FILE_STR
	            ", ?, " JSTATUS_PENDING_STR ", ?, NULL, '', ''"
	            " FROM problems WHERE id=?")
	   .bindAndExecute(priority(JobType::DELETE_FILE), mysql_date(),
	                   problem_id);

	// Add jobs to delete problem submissions' files
	mysql
	   .prepare("INSERT INTO jobs(file_id, creator, type, priority, status,"
	            " added, aux_id, info, data) "
	            "SELECT file_id, NULL, " JTYPE_DELETE_FILE_STR
	            ", ?, " JSTATUS_PENDING_STR ", ?, NULL, '', ''"
	            " FROM submissions WHERE problem_id=?")
	   .bindAndExecute(priority(JobType::DELETE_FILE), mysql_date(),
	                   problem_id);

	// Delete problem (all necessary actions will take plate thanks to foreign
	// key constrains)
	mysql.prepare("DELETE FROM problems WHERE id=?").bindAndExecute(problem_id);

	job_done();

	transaction.commit();
}