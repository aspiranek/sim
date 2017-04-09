#include <sim/mysql.h>
#include <simlib/config_file.h>

namespace MySQL {

Connection makeConnWithCredFile(CStringView filename) {
	ConfigFile cf;
	cf.addVars("user", "host", "password", "host");
	cf.loadConfigFromFile(filename);

	for (auto it : cf.getVars()) {
		if (it.second.isArray())
			THROW("Simfile: variable `", it.first, "` cannot be specified as an"
				" array");
		if (not it.second.isSet())
			THROW("Simfile: variable `", it.first, "` is not set");
	}

	// Connect
	try {
		Connection conn;
		conn.connect(cf["host"].asString(), cf["user"].asString(),
			cf["password"].asString(), cf["database"].asString());
		return conn;

	} catch (const std::exception& e) {
		THROW("Failed to connect to database - ", e.what());
	}
}

} // namespace MySQL
