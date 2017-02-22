#include "errors.h"

void Errors::errorTemplate(const StringView& status, const char* code,
	const char* message)
{
	resp.status_code = status.to_string();
	resp.headers.clear();

	std::string prev = req->headers.get("Referer");
	if (prev.empty())
		prev = '/';

	baseTemplate(status);
	append("<center>"
		"<h1 style=\"font-size:25px;font-weight:normal;\">", code, " &mdash; ",
			message, "</h1>"
		"<a class=\"btn\" href=\"", prev, "\">Go back</a>"
		"</center>");
}
