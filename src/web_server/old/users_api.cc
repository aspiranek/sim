#include "sim/is_username.hh"
#include "sim/jobs/utils.hh"
#include "sim/users/user.hh"
#include "simlib/random.hh"
#include "simlib/sha.hh"
#include "simlib/string_transform.hh"
#include "simlib/time.hh"
#include "simlib/utilities.hh"
#include "src/web_server/old/sim.hh"

#include <cstdint>
#include <limits>
#include <type_traits>

using sim::jobs::Job;
using sim::users::User;
using std::string;

namespace web_server::old {

void Sim::api_user() {
    STACK_UNWINDING_MARK;

    if (not session.has_value()) {
        return api_error403();
    }

    StringView next_arg = url_args.extract_next_arg();
    if (not is_digit(next_arg) or request.method != http::Request::POST) {
        return api_error400();
    }

    auto uid_opt = str2num<decltype(users_uid)>(next_arg);
    if (not uid_opt) {
        return api_error404();
    }
    users_uid = *uid_opt;
    users_perms = users_get_permissions(users_uid);
    if (users_perms == UserPermissions::NONE) {
        return api_error404();
    }

    next_arg = url_args.extract_next_arg();
    if (next_arg == "merge_into_another") {
        return api_user_merge_into_another();
    }
    return api_error404();
}

void Sim::api_user_merge_into_another() {
    STACK_UNWINDING_MARK;

    auto donor_user_id = users_uid;
    if (uint(~users_perms & UserPermissions::MERGE)) {
        return api_error403();
    }

    InplaceBuff<32> target_user_id;
    form_validate_not_blank(target_user_id, "target_user", "Target user ID",
            is_digit_not_greater_than<std::numeric_limits<
                    decltype(sim::jobs::MergeUsersInfo::target_user_id)>::max()>);

    if (notifications.size) {
        return api_error400(notifications);
    }

    if (donor_user_id == str2num<decltype(donor_user_id)>(target_user_id)) {
        return api_error400("You cannot merge user with themselves");
    }

    auto target_user_perms = [&] {
        if (auto uid_opt = str2num<decltype(User::id)>(target_user_id)) {
            return users_get_permissions(*uid_opt);
        }
        return UserPermissions::NONE;
    }();
    if (uint(~target_user_perms & UserPermissions::MERGE)) {
        return api_error403("You do not have permission to merge to the target user");
    }

    if (not check_submitted_password()) {
        return api_error403("Invalid password");
    }

    // Queue merging job
    auto stmt = mysql.prepare("INSERT jobs (creator, status, priority, type,"
                              " added, aux_id, info, data)"
                              "VALUES(?, ?, ?, ?, ?, ?, ?, '')");
    stmt.bind_and_execute(session->user_id, EnumVal(Job::Status::PENDING),
            default_priority(Job::Type::MERGE_USERS), EnumVal(Job::Type::MERGE_USERS),
            mysql_date(), donor_user_id,
            sim::jobs::MergeUsersInfo(
                    WONT_THROW(str2num<decltype(sim::jobs::MergeUsersInfo::target_user_id)>(
                            target_user_id)
                                       .value()))
                    .dump());

    sim::jobs::notify_job_server();
    append(stmt.insert_id());
}

} // namespace web_server::old
