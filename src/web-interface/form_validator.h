#pragma once

#include "http_request.h"

#include <simlib/utilities.h>

class FormValidator {
private:
	const server::HttpRequest::Form& form_;
	std::string errors_;

public:
	explicit FormValidator(const server::HttpRequest::Form& form)
			: form_(form), errors_() {}

	~FormValidator() {}

	// Returns true if and only if field @p var exists and its value is
	// no longer than max_size
	bool validate(std::string& var, const std::string& name,
			const std::string& name_to_print, size_t max_size = -1);

	// validate field + (if not blank) check it by comp
	template<class Checker>
	bool validate(std::string& var, const std::string& name,
			const std::string& name_to_print, Checker&& check,
			const std::string& error, size_t max_size = -1);

	// Like validate() but also validate not blank
	bool validateNotBlank(std::string& var, const std::string& name,
			const std::string& name_to_print, size_t max_size = -1);

	// validate field + check it by comp
	template<class Checker>
	bool validateNotBlank(std::string& var, const std::string& name,
			const std::string& name_to_print, Checker&& check,
			const std::string& error, size_t max_size = -1);

	bool exist(const std::string& name) {
		return 0 != form_.other.count(name);
	}

	void addError(const std::string& error) {
		back_insert(errors_, "<pre class=\"error\">", htmlEscape(error),
			"</pre>");
	}

	/// @brief Returns path of file of form name @p name or empty string if such
	/// does not exist
	std::string getFilePath(const std::string& name) const {
		auto const& form = form_.files;
		auto it = form.find(name);
		return (it == form.end() ? "" : it->second);
	}

	/// @brief Sets path of file of form name @p name to @p var or sets an error
	/// if such does not exist
	bool validateFilePathNotEmpty(std::string& var, const std::string& name,
		const std::string& name_to_print)
	{
		auto const& form = form_.files;
		auto it = form.find(name);
		if (it == form.end()) {
			back_insert(errors_, "<pre class=\"error\">",
				htmlEscape(name_to_print),
				" has to be submitted as a file</pre>");
			return false;
		}

		var = it->second;
		return true;
	}

	/// @brief Returns value of variable @p name or empty string if such does
	/// not exist
	std::string get(const std::string& name) const {
		auto const& form = form_.other;
		auto it = form.find(name);
		return (it == form.end() ? "" : it->second);
	}

	std::string errors() const {
		return (errors_.empty() ? ""
			: concat("<div class=\"notifications\">", errors_, "</div>"));
	}

	bool noErrors() const { return errors_.empty(); }
};

// validate field + (if not blank) check it by comp
template<class Checker>
inline bool FormValidator::validate(std::string& var, const std::string& name,
	const std::string& name_to_print, Checker&& check, const std::string& error,
	size_t max_size)
{
	if (validate(var, name, name_to_print, max_size)) {
		if (var.empty() || check(var))
			return true;

		back_insert(errors_, "<pre class=\"error\">",
			htmlEscape(error.empty()
				? (name_to_print + " validation error") : error), "</pre>");
	}

	return false;
}

// Like validate but also validate not blank
inline bool FormValidator::validateNotBlank(std::string& var,
	const std::string& name, const std::string& name_to_print, size_t max_size)
{
	if (validate(var, name, name_to_print, max_size) && var.empty()) {
		back_insert(errors_, "<pre class=\"error\">",
			htmlEscape(name_to_print), " cannot be blank</pre>");
		return false;
	}

	return true;
}

// validate field + check it by comp
template<class Checker>
inline bool FormValidator::validateNotBlank(std::string& var,
	const std::string& name, const std::string& name_to_print, Checker&& check,
	const std::string& error, size_t max_size)
{
	if (validateNotBlank(var, name, name_to_print, max_size)) {
		if (check(var))
			return true;

		back_insert(errors_, "<pre class=\"error\">",
			htmlEscape(error.empty()
				? (name_to_print + " validation error") : error), "</pre>");
	}

	return false;
}
