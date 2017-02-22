#include <cppconn/driver.h>
#include <mutex>
#include <sim/mysql.h>
#include <simlib/debug.h>
#include <simlib/logger.h>

using std::string;
using std::unique_ptr;

namespace MySQL {

Connection::Connection(const string& host, const string& user,
	const string& password, const string& database)
	: conn_(), host_(host), user_(user), password_(password),
		database_(database)
{
	reconnect();
}

void Connection::reconnect() {
	// We have to serialize the creation of connections, because MySQL Connector
	// C++ would probably crush if we didn't guard it
	static std::mutex lock;
	try {
		std::lock_guard<std::mutex> guard(lock);
		conn_.reset(get_driver_instance()->connect(host_, user_, password_));
		bad_state = false;

	} catch (...) {
		bad_state = true;
		throw;
	}

	conn_->setSchema(database_);
}

Connection createConnectionUsingPassFile(const CStringView& filename) {
	char *host = nullptr, *user = nullptr, *password = nullptr,
		*database = nullptr;

	FILE *conf = fopen(filename.c_str(), "r");
	if (conf == nullptr)
		THROW("Cannot open file: '", filename, '\'', error(errno));

	// Get credentials
	size_t x1 = 0, x2 = 0, x3 = 0, x4 = 0;
	if (getline(&user, &x1, conf) == -1 ||
		getline(&password, &x2, conf) == -1 ||
		getline(&database, &x3, conf) == -1 ||
		getline(&host, &x4, conf) == -1)
	{
		// Free resources
		fclose(conf);
		free(host);
		free(user);
		free(password);
		free(database);

		THROW("Failed to get database config");
	}

	fclose(conf);
	user[__builtin_strlen(user) - 1] = '\0';
	password[__builtin_strlen(password) - 1] = '\0';
	database[__builtin_strlen(database) - 1] = '\0';
	host[__builtin_strlen(host) - 1] = '\0';

	unique_ptr<char, delete_using_free<char>> f1(user), f2(password),
		f3(database), f4(host);

	// Connect
	try {
		return Connection(host, user, password, database);

	} catch (const std::exception& e) {
		THROW("Failed to connect to database - ", e.what());

	} catch (...) {
		THROW("Failed to connect to database");
	}
}

} // namespace MySQL
