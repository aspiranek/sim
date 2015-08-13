#include "form_validator.h"
#include "sim_user.h"
#include "sim_session.h"
#include "sim_template.h"

#include "../include/debug.h"
#include "../include/sha.h"

using std::string;
using std::map;

struct Sim::User::Data {
	string user_id, username, first_name, last_name, email;
	unsigned user_type;
	enum ViewType { FULL, READ_ONLY } view_type;
};

class Sim::User::TemplateWithMenu : public Template {
public:
	TemplateWithMenu(Sim& sim, const string& user_id, const std::string& title,
		const std::string& styles = "", const std::string& scripts = "");

	void printUser(const Data& data);
};

Sim::User::TemplateWithMenu::TemplateWithMenu(Sim& sim, const string& user_id,
	const string& title, const string& styles, const string& scripts)
		: Sim::Template(sim, title, ".body{margin-left:190px}" + styles,
			scripts) {
	if (sim.session->open() != Session::OK)
		return;

	*this << "<ul class=\"menu\">\n"
			"<span>YOUR ACCOUNT</span>"
			"<a href=\"/u/" << sim.session->user_id << "\">Edit profile</a>\n"
			"<a href=\"/u/" << sim.session->user_id << "/change-password\">"
				"Change password</a>\n";

	if (sim.session->user_id != user_id)
		*this << "<span>VIEWED ACCOUNT</span>"
			"<a href=\"/u/" << user_id << "\">Edit profile</a>\n"
			"<a href=\"/u/" << user_id << "/change-password\">Change password"
				"</a>\n";

	*this << "</ul>";
}

void Sim::User::TemplateWithMenu::printUser(const Data& data) {
	*this << "<h4><a href=\"/u/" << data.user_id << "\">" << data.username
		<< "</a>" << " (" << data.first_name << " " << data.last_name
		<< ")</h4>\n";
}

void Sim::User::handle() {
	if (sim_.session->open() != Session::OK)
		return sim_.redirect("/login" + sim_.req_->target);

	size_t arg_beg = 3;
	Data data;

	// Extract user id
	int res_code = strToNum(data.user_id, sim_.req_->target, arg_beg, '/');
	if (res_code == -1)
		return sim_.error404();

	arg_beg += res_code + 1;

	// Get user information
	sqlite3_stmt *stmt;
	if (sqlite3_prepare_v2(sim_.db, "SELECT username, first_name, last_name, "
		"email, type FROM users WHERE id=?", -1, &stmt, NULL))
		return sim_.error500();

	sqlite3_bind_text(stmt, 1, data.user_id.c_str(), -1, SQLITE_STATIC);

	if (sqlite3_step(stmt) == SQLITE_ROW) {
		data.username = (const char*)sqlite3_column_text(stmt, 0);
		data.first_name = (const char*)sqlite3_column_text(stmt, 1);
		data.last_name = (const char*)sqlite3_column_text(stmt, 2);
		data.email = (const char*)sqlite3_column_text(stmt, 3);
		data.user_type = sqlite3_column_int(stmt, 4);
		sqlite3_finalize(stmt);
	} else {
		sqlite3_finalize(stmt);
		return sim_.error404();
	}

	data.view_type = Data::FULL;
	/* Check permissions
	* +---------------+---------+-------+---------+--------+
	* | user \ viewer | id == 1 | admin | teacher | normal |
	* +---------------+---------+-------+---------+--------+
	* |    normal     |  FULL   | FULL  |   RDO   |  ---   |
	* |    teacher    |  FULL   | FULL  |   ---   |  ---   |
	* |     admin     |  FULL   |  RDO  |   ---   |  ---   |
	* +---------------+---------+-------+---------+--------+
	*/
	if (data.user_id == sim_.session->user_id) {}

	else if (sim_.session->user_type >= 2 ||
			(sim_.session->user_type == 1 && data.user_type < 2))
		return sim_.error403();

	else if (sim_.session->user_type == 1 ||
				(sim_.session->user_type == 0 && sim_.session->user_id != "1"))
		data.view_type = Data::READ_ONLY;

	// Change password
	if (compareTo(sim_.req_->target, arg_beg, '/', "change-password") == 0)
		return changePassword(data);

	// Delete account
	if (compareTo(sim_.req_->target, arg_beg, '/', "delete") == 0)
		return deleteAccount(data);

	// Edit account
	editProfile(data);
}

// Krzysztof Małysa
#include <bits/stdc++.h>
#include <unistd.h>

using namespace std;

#define FOR(i,a,n) for (int i = (a), __n ## i = n; i < __n ## i; ++i)
#define REP(i,n) FOR(i,0,n)
#define FORD(i,a,n) for (int i = (a), __n ## i = n; i >= __n ## i; --i)
#define LET(x,a) __typeof(a) x = (a)
#define FOREACH(i,x) for (LET(i, x.begin()), __n##i = x.end(); i != __n##i; ++i)
#define ALL(x) x.begin(), x.end()
#define SZ(x) (int(x.size()))
#define eprintf(...) fprintf(stderr, __VA_ARGS__)
#define ST first
#define ND second
#define MP make_pair
#define PB push_back
#define O(...) ostream& operator <<(ostream& os, const __VA_ARGS__& x)
#define OO(...) O(__VA_ARGS__) { return __out(os, ALL(x)); }
#define T template
#define CL class

typedef unsigned uint;
typedef long long LL;
typedef unsigned long long ULL;
typedef vector<int> VI;
typedef vector<VI> VVI;
typedef vector<LL> VLL;
typedef pair<int, int> PII;
typedef vector<PII> VPII;
typedef vector<VPII> VVPII;
typedef pair<LL, LL> PLLLL;
typedef vector<PLLLL> VPLLLL;
typedef vector<bool> VB;
typedef vector<char> VC;

T<CL A>
inline A abs(const A& a) { return a < A() ? -a : a; }

T<CL A, CL B>
inline void mini(A& a, const B& b) {
	if (b < a)
		a = b;
}

T<CL A, CL B>
inline void maxi(A& a, const B& b) {
	if (b > a)
		a = b;
}

T<CL Iter>
ostream& __out(ostream& os, Iter a, Iter b, const string& s = ", ");

T<CL A, CL B>
O(pair<A, B>);

T<CL A>
OO(vector<A>)

T<CL A>
OO(deque<A>)

T<CL A>
OO(list<A>)

T<CL A, CL B>
OO(set<A, B>)

T<CL A, CL B, CL C>
OO(map<A, B, C>)

T<CL A, CL B>
OO(multiset<A, B>)

T<CL A, CL B, CL C>
OO(multimap<A, B, C>)

T<CL Iter>
ostream& __out(ostream& os, Iter a, Iter b, const string& s) {
	os << "{";
	if (a != b) {
		os << *a;
		while (++a != b)
			os << s << *a;
	}
	return os << "}";
}

T<CL A, CL B>
O(pair<A, B>) {
	return os << "(" << x.ST << ", " << x.ND << ")";
}

CL Input {
	static const int BUFF_SIZE = 1 << 16;
	unsigned char buff[BUFF_SIZE], *pos, *end;

	void grabBuffer() {
		pos = buff;
		end = buff + read(0, buff, BUFF_SIZE);
	}

public:
	Input() : pos(buff), end(buff) {}

	int peek() {
		if (pos == end)
			grabBuffer();
		return pos != end ? *pos : -1;
	}

	int getChar() {
		if (pos == end)
			grabBuffer();
		return pos != end ? *pos++ : -1;
	}

	void skipWhiteSpaces() {
		while (isspace(peek()))
			++pos;
	}

	T<CL A>
	A get();

	T<CL A>
	void operator()(A& x) { x = get<A>(); }

	T<CL A, CL B>
	void operator()(A& a, B& b) { operator()(a), operator()(b); }

	T<CL A, CL B, CL C>
	void operator()(A& a, B& b, C& c) { operator()(a, b), operator()(c); }

	T<CL A, CL B, CL C, CL D>
	void operator()(A& a, B& b, C& c, D& d) {
		operator()(a, b, c);
		operator()(d);
	}

	T<CL A, CL B, CL C, CL D, CL E>
	void operator()(A& a, B& b, C& c, D& d, E& e) {
		operator()(a, b, c, d);
		operator()(e);
	}

	T<CL A, CL B, CL C, CL D, CL E, CL F>
	void operator()(A& a, B& b, C& c, D& d, E& e, F& f) {
		operator()(a, b, c, d, e);
		operator()(f);
	}
} input;


T<> uint Input::get<uint>() {
	skipWhiteSpaces();
	uint x = 0;
	while (isdigit(peek()))
		x = x * 10 + *pos++ - '0';
	return x;
}

T<> int Input::get<int>() {
	skipWhiteSpaces();
	return peek() == '-' ? (++pos, -get<uint>()) : get<uint>();
}

T<> ULL Input::get<ULL>() {
	skipWhiteSpaces();
	ULL x = 0;
	while (isdigit(peek()))
		x = x * 10 + *pos++ - '0';
	return x;
}

T<> LL Input::get<LL>() {
	skipWhiteSpaces();
	return peek() == '-' ? (++pos, -get<ULL>()) : get<ULL>();
}

T<> string Input::get<string>() {
	skipWhiteSpaces();
	string x;
	while (!isspace(peek()))
		x += *pos++;
	return x;
}

#undef T
#undef CL
#ifdef DEBUG
# define D(...) __VA_ARGS__
# define E(...) eprintf(__VA_ARGS__)
# define OUT(a,b) cerr << #a ":", __out(cerr, a, b), E("\n")
# define LOG(x) cerr << #x ": " << (x)
# define LOG2(x, y) cerr << x ": " << (y)
# define LOGN(x) LOG(x) << endl
# define LOGN2(x, y) LOG2(x, y) << endl
#else
# define D(...)
# define E(...)
# define OUT(...)
# define LOG(...)
# define LOG2(...)
# define LOGN(...)
# define LOGN2(...)
#endif
/// End of templates



void Sim::User::login() {
	FormValidator fv(sim_.req_->form_data);
	string username;

	if (sim_.req_->method == server::HttpRequest::POST) {
		// Try to login
		string password;
		// Validate all fields
		fv.validate(username, "username", "Username", 30);
		fv.validate(password, "password", "Password");

		if (fv.noErrors()) {
			sqlite3_stmt *stmt;
			if (sqlite3_prepare_v2(sim_.db, "SELECT id FROM users "
					"WHERE username=? AND password=?", -1, &stmt, NULL))
				return sim_.error500();

			sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt, 2, sha256(password).c_str(), -1,
				SQLITE_TRANSIENT);

			if (sqlite3_step(stmt) == SQLITE_ROW) {
				// Delete old sessions
				sqlite3_exec(sim_.db, "BEGIN", NULL, NULL, NULL);
				sim_.session->open();
				sim_.session->destroy();
				// Create new
				sim_.session->create((const char*)sqlite3_column_text(stmt, 0));
				sqlite3_exec(sim_.db, "COMMIT", NULL, NULL, NULL);

				sqlite3_finalize(stmt);

				// If there is redirection string, redirect
				if (sim_.req_->target.size() > 6)
					return sim_.redirect(sim_.req_->target.substr(6));

				return sim_.redirect("/");
			}

			sqlite3_finalize(stmt);
		}
	}

	Template templ(sim_, "Login");
	templ << fv.errors() << "<div class=\"form-container\">\n"
			"<h1>Log in</h1>\n"
			"<form method=\"post\">\n"
				// Username
				"<div class=\"field-group\">\n"
					"<label>Username</label>\n"
					"<input type=\"text\" name=\"username\" value=\""
						<< htmlSpecialChars(username) << "\" size=\"24\" "
						"maxlength=\"30\" required>\n"
				"</div>\n"
				// Password
				"<div class=\"field-group\">\n"
					"<label>Password</label>\n"
					"<input type=\"password\" name=\"password\" "
						"size=\"24\">\n"
				"</div>\n"
				"<input class=\"btn\" type=\"submit\" value=\"Log in\">\n"
			"</form>\n"
		"</div>\n";
}

void Sim::User::logout() {
	sim_.session->open();
	sim_.session->destroy();
	sim_.redirect("/login");
}

void Sim::User::signUp() {
	if (sim_.session->open() == Session::OK)
		return sim_.redirect("/");

	FormValidator fv(sim_.req_->form_data);
	string username, first_name, last_name, email, password1, password2;

	if (sim_.req_->method == server::HttpRequest::POST) {
		// Validate all fields
		fv.validateNotBlank(username, "username", "Username", 30);
		fv.validateNotBlank(first_name, "first_name", "First Name");
		fv.validateNotBlank(last_name, "last_name", "Last Name", 60);
		fv.validateNotBlank(email, "email", "Email", 60);
		if (fv.validate(password1, "password1", "Password") &&
				fv.validate(password2, "password2", "Password (repeat)") &&
				password1 != password2)
			fv.addError("Passwords don't match");

		// If all fields are ok
		if (fv.noErrors()) {
			sqlite3_stmt *stmt;
			if (sqlite3_prepare_v2(sim_.db, "INSERT OR IGNORE INTO `users` "
					"(username, first_name, last_name, email, password) "
					"VALUES(?, ?, ?, ?, ?)", -1, &stmt, NULL))
				return sim_.error500();

			sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt, 2, first_name.c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt, 3, last_name.c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt, 4, email.c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt, 5, sha256(password1).c_str(), -1,
				SQLITE_TRANSIENT);

			if (sqlite3_step(stmt) == SQLITE_DONE) {
				sqlite3_finalize(stmt);
				if (sqlite3_prepare_v2(sim_.db, "SELECT id FROM users "
						"WHERE username=?", -1, &stmt, NULL))
					return sim_.error500();

				sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);

				if (sqlite3_step(stmt) == SQLITE_ROW) {
					sim_.session->create((const char*)sqlite3_column_text(stmt,
						0));
					sqlite3_finalize(stmt);
					return sim_.redirect("/");
				}

			} else
				fv.addError("Username taken");

			sqlite3_finalize(stmt);
		}
	}

	Template templ(sim_, "Register");
	templ << fv.errors() << "<div class=\"form-container\">\n"
			"<h1>Register</h1>\n"
			"<form method=\"post\">\n"
				// Username
				"<div class=\"field-group\">\n"
					"<label>Username</label>\n"
					"<input type=\"text\" name=\"username\" value=\""
						<< htmlSpecialChars(username) << "\" size=\"24\" "
						"maxlength=\"30\" required>\n"
				"</div>\n"
				// First Name
				"<div class=\"field-group\">\n"
					"<label>First name</label>\n"
					"<input type=\"text\" name=\"first_name\" value=\""
						<< htmlSpecialChars(first_name) << "\" size=\"24\" "
						"maxlength=\"60\" required>\n"
				"</div>\n"
				// Last name
				"<div class=\"field-group\">\n"
					"<label>Last name</label>\n"
					"<input type=\"text\" name=\"last_name\" value=\""
						<< htmlSpecialChars(last_name) << "\" size=\"24\" "
						"maxlength=\"60\" required>\n"
				"</div>\n"
				// Email
				"<div class=\"field-group\">\n"
					"<label>Email</label>\n"
					"<input type=\"email\" name=\"email\" value=\""
						<< htmlSpecialChars(email) << "\" size=\"24\" "
						"maxlength=\"60\" required>\n"
				"</div>\n"
				// Password
				"<div class=\"field-group\">\n"
					"<label>Password</label>\n"
					"<input type=\"password\" name=\"password1\" size=\"24\">\n"
				"</div>\n"
				// Password (repeat)
				"<div class=\"field-group\">\n"
					"<label>Password (repeat)</label>\n"
					"<input type=\"password\" name=\"password2\" size=\"24\">\n"
				"</div>\n"
				"<input class=\"btn\" type=\"submit\" value=\"Sign up\">\n"
			"</form>\n"
		"</div>\n";
}

void Sim::User::changePassword(Data& data) {
	if (data.view_type == Data::READ_ONLY)
		return sim_.error403();

	FormValidator fv(sim_.req_->form_data);
	if (sim_.req_->method == server::HttpRequest::POST) {
		// Validate all fields
		string old_password, password1, password2;
		fv.validate(old_password, "old_password", "Old password");
		if (fv.validate(password1, "password1", "New password") &&
				fv.validate(password2, "password2", "New password (repeat)") &&
				password1 != password2)
			fv.addError("Passwords don't match");

		// If all fields are ok
		if (fv.noErrors()) {
			sqlite3_stmt *stmt;
			if (sqlite3_prepare_v2(sim_.db, "SELECT password FROM users "
					"WHERE id=?", -1, &stmt, NULL))
				return sim_.error500();

			sqlite3_bind_text(stmt, 1, data.user_id.c_str(), -1, SQLITE_STATIC);

			if (sqlite3_step(stmt) != SQLITE_ROW)
				fv.addError("Cannot get user password");

			else if (sha256(old_password) != (const char*)sqlite3_column_text(
					stmt, 0))
				fv.addError("Wrong password");

			else {
				sqlite3_finalize(stmt);
				if (sqlite3_prepare_v2(sim_.db, "UPDATE users SET password=? "
						"WHERE id=?", -1, &stmt, NULL))
					return sim_.error500();
				sqlite3_bind_text(stmt, 1, sha256(password1).c_str(), -1,
					SQLITE_TRANSIENT);
				sqlite3_bind_text(stmt, 2, data.user_id.c_str(), -1,
					SQLITE_STATIC);

				if (sqlite3_step(stmt) == SQLITE_DONE ||
						old_password == password1)
					fv.addError("Update successful");
				else
					fv.addError("Update failed");
			}
			sqlite3_finalize(stmt);
		}
	}

	TemplateWithMenu templ(sim_, data.user_id, "Change password");
	templ.printUser(data);
	templ << fv.errors() << "<div class=\"form-container\">\n"
			"<h1>Change password</h1>\n"
			"<form method=\"post\">\n"
				// Old password
				"<div class=\"field-group\">\n"
					"<label>Old password</label>\n"
					"<input type=\"password\" name=\"old_password\" "
						"size=\"24\">\n"
				"</div>\n"
				// New password
				"<div class=\"field-group\">\n"
					"<label>New password</label>\n"
					"<input type=\"password\" name=\"password1\" size=\"24\">\n"
				"</div>\n"
				// New password (repeat)
				"<div class=\"field-group\">\n"
					"<label>New password (repeat)</label>\n"
					"<input type=\"password\" name=\"password2\" size=\"24\">\n"
				"</div>\n"
				"<input class=\"btn\" type=\"submit\" value=\"Update\">\n"
			"</form>\n"
		"</div>\n";
}

void Sim::User::editProfile(Data& data) {
	// The ability to change user type
	bool can_change_user_type = data.user_id != "1" &&
		sim_.session->user_type == 0 && (data.user_type > 0 ||
			sim_.session->user_id == "1");

	FormValidator fv(sim_.req_->form_data);
	if (sim_.req_->method == server::HttpRequest::POST &&
			data.view_type == Data::FULL) {
		string new_username, user_type;
		// Validate all fields
		fv.validateNotBlank(new_username, "username", "Username", 30);
		fv.validateNotBlank(user_type, "type", "Account type");
		fv.validateNotBlank(data.first_name, "first_name", "First Name");
		fv.validateNotBlank(data.last_name, "last_name", "Last Name", 60);
		fv.validateNotBlank(data.email, "email", "Email", 60);

		// If all fields are ok
		if (fv.noErrors()) {
			sqlite3_stmt *stmt;
			if (sqlite3_prepare_v2(sim_.db, "UPDATE IGNORE users "
					"SET username=?, first_name=?, last_name=?, email=?, "
					"type=? WHERE id=?", -1, &stmt, NULL))
				return sim_.error500();

			sqlite3_bind_text(stmt, 1, new_username.c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt, 2, data.first_name.c_str(), -1,
				SQLITE_STATIC);
			sqlite3_bind_text(stmt, 3, data.last_name.c_str(), -1,
				SQLITE_STATIC);
			sqlite3_bind_text(stmt, 4, data.email.c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_int(stmt, 5, can_change_user_type ? (user_type == "0"
				? 0 : user_type == "1" ? 1 : 2) : data.user_type);
			sqlite3_bind_text(stmt, 6, data.user_id.c_str(), -1, SQLITE_STATIC);

			if (sqlite3_step(stmt) == SQLITE_DONE) {
				fv.addError("Update successful");
				// Update user info
				data.username = new_username;
				data.user_type = (user_type == "0" ? 0
					: user_type == "1" ? 1 : 2);

				if (data.user_id == sim_.session->user_id)
					// We do not have to actualise user_type because nobody
					// can change their account type
					sim_.session->username = new_username;

			} else if (data.username != new_username)
				fv.addError("Username '" + new_username + "' is taken");

			sqlite3_finalize(stmt);
		}
	}

	TemplateWithMenu templ(sim_, data.user_id, "Edit profile");
	templ.printUser(data);
	templ << fv.errors() << "<div class=\"form-container\">\n"
		"<h1>Edit account</h1>\n"
		"<form method=\"post\">\n"
			// Username
			"<div class=\"field-group\">\n"
				"<label>Username</label>\n"
				"<input type=\"text\" name=\"username\" value=\""
					<< htmlSpecialChars(data.username) << "\" size=\"24\" "
					"maxlength=\"30\" " << (data.view_type == Data::READ_ONLY ?
						"readonly" : "required") <<  ">\n"
			"</div>\n"
			// Account type
			"<div class=\"field-group\">\n"
				"<label>Account type</label>\n"
				"<select name=\"type\"" << (can_change_user_type ? ""
					: " disabled") << ">"
					"<option value=\"0\"" << (data.user_type == 0 ? " selected"
						: "") << ">Admin</option>"
					"<option value=\"1\"" << (data.user_type == 1 ? " selected"
						: "") << ">Teacher</option>"
					"<option value=\"2\"" << (data.user_type > 1 ? " selected"
						: "") << ">Normal</option>"
				"</select>\n"
			"</div>\n"
			// First Name
			"<div class=\"field-group\">\n"
				"<label>First name</label>\n"
				"<input type=\"text\" name=\"first_name\" value=\""
					<< htmlSpecialChars(data.first_name) << "\" size=\"24\""
					"maxlength=\"60\" " << (data.view_type == Data::READ_ONLY ?
						"readonly" : "required") <<  ">\n"
			"</div>\n"
			// Last name
			"<div class=\"field-group\">\n"
				"<label>Last name</label>\n"
				"<input type=\"text\" name=\"last_name\" value=\""
					<< htmlSpecialChars(data.last_name) << "\" size=\"24\""
					"maxlength=\"60\" " << (data.view_type == Data::READ_ONLY ?
						"readonly" : "required") <<  ">\n"
			"</div>\n"
			// Email
			"<div class=\"field-group\">\n"
				"<label>Email</label>\n"
				"<input type=\"email\" name=\"email\" value=\""
					<< htmlSpecialChars(data.email) << "\" size=\"24\""
					"maxlength=\"60\" " << (data.view_type == Data::READ_ONLY ?
						"readonly" : "required") <<  ">\n"
			"</div>\n";

	// Buttons
	if (data.view_type == Data::FULL)
		templ << "<div>\n"
				"<input class=\"btn\" type=\"submit\" value=\"Update\">\n"
				"<a class=\"btn-danger\" style=\"float:right\" href=\"/u/"
					<< data.user_id << "/delete\">Delete account</a>\n"
			"</div>\n";

	templ << "</form>\n"
		"</div>\n";
}

void Sim::User::deleteAccount(Data& data) {
	FormValidator fv(sim_.req_->form_data);
	// Deleting "root" account (id 1) is forbidden
	if (data.user_id == "1") {
		TemplateWithMenu templ(sim_, data.user_id, "Delete account");
		templ.printUser(data);
		templ << "<h1>You cannot delete SIM root account</h1>";
		return;
	}

	if (data.view_type == Data::READ_ONLY)
		return sim_.error403();

	if (sim_.req_->method == server::HttpRequest::POST)
		if (fv.exist("delete")) {
			sqlite3_stmt *stmt;
			sqlite3_exec(sim_.db, "BEGIN", NULL, NULL, NULL);
			// Change contests and problems owner id to 1
			if (sqlite3_prepare_v2(sim_.db, "UPDATE rounds r, problems p "
					"SET r.owner=1, p.owner=1 "
					"WHERE r.owner=? OR p.owner=?", -1, &stmt, NULL)) {
				sqlite3_exec(sim_.db, "ROLLBACK", NULL, NULL, NULL);
				return sim_.error500();
			}

			sqlite3_bind_text(stmt, 1, data.user_id.c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt, 2, data.user_id.c_str(), -1, SQLITE_STATIC);

			sqlite3_step(stmt);
			sqlite3_finalize(stmt);

			// Delete submissions
			if (sqlite3_prepare_v2(sim_.db,
					"DELETE FROM submissions, submissions_to_rounds "
					"USING submissions INNER JOIN submissions_to_rounds " // WARNING!!! - TODO
					"WHERE submissions.user_id=? AND id=submission_id",
					-1, &stmt, NULL)) {
				sqlite3_exec(sim_.db, "ROLLBACK", NULL, NULL, NULL);
				return sim_.error500();
			}

			sqlite3_bind_text(stmt, 1, data.user_id.c_str(), -1, SQLITE_STATIC);

			sqlite3_step(stmt);
			sqlite3_finalize(stmt);

			// Delete from users_to_contests
			if (sqlite3_prepare_v2(sim_.db, "DELETE FROM users_to_contests "
					"WHERE user_id=?", -1, &stmt, NULL)) {
				sqlite3_exec(sim_.db, "ROLLBACK", NULL, NULL, NULL);
				return sim_.error500();
			}

			sqlite3_bind_text(stmt, 1, data.user_id.c_str(), -1, SQLITE_STATIC);

			sqlite3_step(stmt);
			sqlite3_finalize(stmt);

			// Delete user
			if (sqlite3_prepare_v2(sim_.db, "DELETE FROM users WHERE id=?", -1,
					&stmt, NULL)) {
				sqlite3_exec(sim_.db, "ROLLBACK", NULL, NULL, NULL);
				return sim_.error500();
			}

			sqlite3_bind_text(stmt, 1, data.user_id.c_str(), -1, SQLITE_STATIC);

			if (sqlite3_step(stmt) == SQLITE_DONE) {
				if (data.user_id == sim_.session->user_id)
					sim_.session->destroy();

				sqlite3_finalize(stmt);
				sqlite3_exec(sim_.db, "COMMIT", NULL, NULL, NULL);
				return sim_.redirect("/");
			}

			sqlite3_finalize(stmt);
			sqlite3_exec(sim_.db, "COMMIT", NULL, NULL, NULL);
		}

	TemplateWithMenu templ(sim_, data.user_id, "Delete account");
	templ.printUser(data);
	templ << fv.errors() << "<div class=\"form-container\">\n"
			"<h1>Delete account</h1>\n"
			"<form method=\"post\">\n"
				"<label class=\"field\">Are you sure to delete account "
					"<a href=\"/u/" << data.user_id << "\">"
					<< htmlSpecialChars(data.username) << "</a>, all its "
					"submissions and change owner of its contests and "
					"problems to SIM root?</label>\n"
				"<div class=\"submit-yes-no\">\n"
					"<button class=\"btn-danger\" type=\"submit\" "
						"name=\"delete\">Yes, I'm sure</button>\n"
					"<a class=\"btn\" href=\"/u/" << data.user_id << "\">"
						"No, go back</a>\n"
				"</div>\n"
			"</form>\n"
		"</div>\n";
}
