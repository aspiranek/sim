#pragma once

#include "cppconn_bug_fix.h"

#include <cppconn/prepared_statement.h>
#include <mysql_connection.h>
#include <simlib/memory.h>
#include <simlib/string.h>

namespace MySQL {

class Result {
private:
	std::unique_ptr<sql::ResultSet> res_;

	explicit Result(sql::ResultSet* res) : res_(res) {}

public:
	Result() = default;

	Result(const Result&) = delete;

	Result(Result&&) noexcept = default;

	Result& operator=(const Result&) = delete;

	Result& operator=(Result&&) noexcept = default;

	~Result() = default;

	sql::ResultSet* impl() { return res_.get(); }

	bool next() { return res_->next(); }

	bool previous() { return res_->previous(); }

	size_t rowCount() const { return res_->rowsCount(); }

	bool isNull(uint index) const { return res_->isNull(index); }

	bool getBool(uint index) const { return res_->getBoolean(index); }

	int32_t getInt(uint index) const { return res_->getInt(index); }

	uint32_t getUInt(uint index) const { return res_->getUInt(index); }

	int64_t getInt64(uint index) const { return res_->getInt64(index); }

	uint64_t getUInt64(uint index) const { return res_->getUInt64(index); }

	long double getDouble(uint index) const { return res_->getDouble(index); }

	std::string getString(uint index) const { return res_->getString(index); }

	std::string operator[](uint index) const { return getString(index); }

	friend class Statement;
	friend class Connection;
};

class Statement {
private:
	std::unique_ptr<sql::PreparedStatement> pstmt_;

	explicit Statement(sql::PreparedStatement* pstmt) : pstmt_(pstmt) {}

public:
	Statement() = default;

	Statement(const Statement&) = delete;

	Statement(Statement&& ps) noexcept : pstmt_(std::move(ps.pstmt_)) {}

	Statement& operator=(const Statement&) = delete;

	Statement& operator=(Statement&& ps) noexcept {
		pstmt_ = std::move(ps.pstmt_);
		return *this;
	}

	~Statement() = default;

	sql::Statement* impl() { return pstmt_.get(); }

	void clearParams() { pstmt_->clearParameters(); }

	void setBool(uint index, bool val) { pstmt_->setBoolean(index, val); }

	void setInt(uint index, int32_t val) { pstmt_->setInt(index, val); }

	void setUInt(uint index, uint32_t val) { pstmt_->setUInt(index, val); }

	void setInt64(uint index, int64_t val) { pstmt_->setInt64(index, val); }

	void setUInt64(uint index, uint64_t val) { pstmt_->setUInt64(index, val); }

	void setString(uint index, const std::string& val) {
		pstmt_->setString(index, val);
	}

	void setNull(uint index) { pstmt_->setNull(index, 0); }

	int executeUpdate() { return pstmt_->executeUpdate(); }

	Result executeQuery() { return Result(pstmt_->executeQuery()); }

	friend class Connection;
};

class Connection {
private:
	std::unique_ptr<sql::Connection> conn_;
	std::string host_, user_, password_, database_;
	bool bad_state = false; // Bad state is detected ONLY if an exception is
	                        // thrown in the (or deeper) methods: prepare(),
	                        // executeUpdate(), executeQuery(); that is why
	                        // after connection resetting, the first call will
	                        // fail - bad_state is then set and reconnection
	                        // will happen during the next try

public:
	Connection() = default;

	Connection(const std::string& host, const std::string& user,
			const std::string& password, const std::string& database);

	Connection(const Connection&) = delete;

	Connection(Connection&&) noexcept = default;

	Connection& operator=(const Connection&) = delete;

	Connection& operator=(Connection&&) = default;

	~Connection() {}

	sql::Connection* impl() {
		if (bad_state)
			reconnect();
		return conn_.get();
	}

	void reconnect();

	bool badState() noexcept { return bad_state; }

private:
	template<class Func>
	auto try_call(Func&& f) -> decltype(f()) {
		try {
			return f();

		} catch (const std::exception& e) {
			if (hasPrefixIn(e.what(), {"Lost connection to MySQL server",
				"MySQL server has gone away"}))
			{
				// Try to deal with problem silently
				reconnect();
				return f();
			}

			bad_state = true;
			throw;
		}
	}

public:
	Statement prepare(const std::string& query) {
		return try_call([&] {
			return Statement(impl()->prepareStatement(query));
		});
	}

	int executeUpdate(const std::string& update_query) {
		return try_call([&] {
			std::unique_ptr<sql::Statement> stmt(impl()->createStatement());
			return stmt->executeUpdate(update_query);
		});
	}

	Result executeQuery(const std::string& query) {
		return try_call([&] {
			std::unique_ptr<sql::Statement> stmt(impl()->createStatement());
			return Result(stmt->executeQuery(query));
		});
	}

	std::string lastInsertId() {
		Result res = executeQuery("SELECT LAST_INSERT_ID()");
		if (!res.next())
			THROW("Failed to select LAST_INSET_ID");
		return res[1];
	}
};

/**
 * @brief Creates Connection using file @p filename
 * @details File format: "USER\nPASSWORD\nDATABASE\nHOST"
 *
 * @param filename file to load credentials from
 *
 * @return Connection object
 *
 * @errors On error throws std::runtime_error
 */
Connection createConnectionUsingPassFile(const CStringView& filename);

} // namespace MySQL
