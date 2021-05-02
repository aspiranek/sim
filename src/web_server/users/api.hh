#pragma once

#include "sim/users/user.hh"
#include "simlib/string_view.hh"
#include "src/web_server/http/response.hh"
#include "src/web_server/web_worker/context.hh"

namespace web_server::users {

http::Response list(web_worker::Context& ctx);

http::Response list_above_id(web_worker::Context& ctx, decltype(sim::users::User::id) user_id);

http::Response list_by_type(web_worker::Context& ctx, StringView user_type_str);

http::Response list_by_type_above_id(
    web_worker::Context& ctx, StringView user_type_str,
    decltype(sim::users::User::id) user_id);

http::Response view(web_worker::Context& ctx, decltype(sim::users::User::id) user_id);

} // namespace web_server::users