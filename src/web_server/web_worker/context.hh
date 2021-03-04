#pragma once

#include "sim/mysql/mysql.hh"
#include "sim/sessions/session.hh"
#include "simlib/string_view.hh"
#include "src/web_server/http/cookies.hh"
#include "src/web_server/http/request.hh"
#include "src/web_server/http/response.hh"

namespace web_server::web_worker {

struct Context {
    const http::Request& request;
    mysql::Connection& mysql;

    struct Session {
        decltype(sim::sessions::Session::id) id;
        decltype(sim::sessions::Session::csrf_token) csrf_token;
        decltype(sim::sessions::Session::user_id) user_id;
        decltype(sim::users::User::type) user_type;
        decltype(sim::users::User::username) username;
        decltype(sim::sessions::Session::data) data;
        decltype(sim::sessions::Session::data) orig_data;
        static constexpr CStringView id_cookie_name = "session";
        static constexpr CStringView csrf_token_cookie_name = "csrf_token";
    };
    std::optional<Session> session;
    http::Cookies cookie_changes;

    void open_session();
    // void create_new_session(); // TODO: implement during refactor of log-in / sign-up
    // void destroy_session(); // TODO: implement during refactor of log-out
    void close_session();

    http::Response response(StringView status_code, StringView content);

    http::Response response_ok(StringView content = "") { return response("200 OK", content); }

    http::Response response_400(StringView content) {
        return response("400 Bad Request", content);
    }

    http::Response response_403(StringView content = "") {
        return response("403 Forbidden", content);
    }

    http::Response response_404(StringView content = "") {
        return response("404 Not Found", content);
    }
};

} // namespace web_server::web_worker