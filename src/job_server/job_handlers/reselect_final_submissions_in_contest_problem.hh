#pragma once

#include "job_handler.hh"

namespace job_server::job_handlers {

class ReselectFinalSubmissionsInContestProblem final : public JobHandler {
    uint64_t contest_problem_id_;

public:
    ReselectFinalSubmissionsInContestProblem(uint64_t job_id, uint64_t contest_problem_id)
    : JobHandler(job_id)
    , contest_problem_id_(contest_problem_id) {}

    void run() final;
};

} // namespace job_server::job_handlers
