#pragma once

#include "http_request.h"
#include "http_response.h"

#include "../include/db.h"
#include "../include/memory.h"

#include "../include/debug.h"
#define DEBUG_ERROR()  E("\e[31m%s:%d Error: %i - %s => %s\e[m\n", __FILE__, __LINE__, sqlite3_errcode(sim_.db), sqlite3_errstr(sqlite3_errcode(sim_.db)), sqlite3_errmsg(sim_.db));

// Every object is independent, thread-safe
class Sim {
private:
	Sim(const Sim&);
	Sim& operator=(const Sim&);

	class Contest;
	class Session;
	class Template;
	class User;

	sqlite3 *db;
	std::string client_ip_;
	const server::HttpRequest* req_;
	server::HttpResponse resp_;
	// Modules
	Contest *contest;
	Session *session;
	User *user;

	// sim_errors.cc
	void error403();

	void error404();

	void error500();

	// sim_main.cc
	void mainPage();

	void getStaticFile();

	/**
	 * @brief Sets headers to make a redirection
	 * @details Does not clear response headers and contents
	 *
	 * @param location URL address where to redirect
	 */
	void redirect(const std::string& location);

	/**
	 * @brief Returns user type
	 *
	 * @param user_id user id
	 * @return user type
	 */
	int getUserType(const char* user_id);

public:
	Sim();

	~Sim();

	/**
	 * @brief Handles request
	 * @details Takes requests, handle it and returns response
	 *
	 * @param client_ip IP address of the client
	 * @param req request
	 *
	 * @return response
	 */
	server::HttpResponse handle(std::string client_ip,
			const server::HttpRequest& req);
};
