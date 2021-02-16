#include "src/job_server/job_handlers/reselect_final_submissions_in_contest_problem.hh"
#include "sim/submission.hh"
#include "src/job_server/main.hh"

namespace job_handlers {

void ReselectFinalSubmissionsInContestProblem::run() {
    STACK_UNWINDING_MARK;

    auto transaction = mysql.start_transaction();

    auto stmt =
        mysql.prepare("SELECT DISTINCT owner, problem_id "
                      "FROM submissions WHERE contest_problem_id=? AND final_candidate=1 "
                      "ORDER by id"); // Order is important so that if the two such jobs are
                                      // running, one would block until the other finishes
    stmt.bind_and_execute(contest_problem_id_);

    MySQL::Optional<uint64_t> sowner;
    uint64_t problem_id = 0;
    stmt.res_bind_all(sowner, problem_id);
    while (stmt.next()) {
        submission::update_final_lock(mysql, sowner, problem_id);
        submission::update_final(mysql, sowner, problem_id, contest_problem_id_, false);
    }

    job_done();

    transaction.commit();
}

} // namespace job_handlers
