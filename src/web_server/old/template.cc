#include "src/web_server/old/sim.hh"
#include "src/web_server/ui_template.hh"

using sim::users::User;

namespace web_server::old {

void Sim::page_template(StringView title, StringView styles) {
    STACK_UNWINDING_MARK;

    begin_ui_template(
        resp,
        {
            .title = title,
            .styles = styles,
            .show_users = session_is_open and
                uint(users_get_overall_permissions() & UserPermissions::VIEW_ALL),
            .show_submissions = session_is_open and
                uint(submissions_get_overall_permissions() & SubmissionPermissions::VIEW_ALL),
            .show_job_queue = session_is_open and
                uint(jobs_get_overall_permissions() & JobPermissions::VIEW_ALL),
            .show_logs = session_is_open and session_user_type == User::Type::ADMIN,
            .session_user_id = session_is_open ? &session_user_id : nullptr,
            .session_user_type = session_is_open ? &session_user_type : nullptr,
            .session_username = session_is_open ? &session_username : nullptr,
            .notifications = notifications,
        });

#ifdef DEBUG
    notifications.clear();
#endif

    page_template_began = true;
}

void Sim::page_template_end() {
    STACK_UNWINDING_MARK;

    if (page_template_began) {
        page_template_began = false;
        end_ui_template(resp);
    }
}

void Sim::error_page_template(StringView status, StringView code, StringView message) {
    STACK_UNWINDING_MARK;

    resp.status_code = status.to_string();
    resp.headers.clear();

    auto prev = request.headers.get("Referer");
    if (prev.empty()) {
        prev = "/";
    }

    page_template(status);
    // clang-format off
    append("<center>"
           "<h1 style=\"font-size:25px;font-weight:normal;\">",
              code, " &mdash; ", message, "</h1>"
           "<a class=\"btn\" href=\"", prev, "\">Go back</a>"
           "</center>");
    // clang-format on
}

} // namespace web_server::old