#include "pqxx_cp.h"

namespace cp {
	std::string serialize(pqxx::result res) {
		// TODO: it can be better
		if (res.empty()) {
			return "{\"result\": []}";
		}
		std::string res_str = "{\"result\": [";
		for (auto const &row: res) {
			res_str += '{';
			for (auto const &field: row) {
				res_str += '"' + std::string(field.name()) + "\": \"" + std::string(field.c_str()) + "\",";
			}
			res_str[res_str.length() - 1] = '}';
			res_str += ',';
		}
		res_str[res_str.length() - 1] = ']';
		res_str += "}";
		return res_str;
	}

	std::string serialize(pqxx::row row) {
		// TODO: it can be better
		if (row.empty()) {
			return "{\"result\": []}";
		}
		std::string res_str = "{\"result\": [";
		res_str += '{';
		for (auto const &field: row) {
			res_str += '"' + std::string(field.name()) + "\": \"" + std::string(field.c_str()) + "\",";
		}
		res_str[res_str.length() - 1] = '}';
		res_str += ',';
		
		res_str[res_str.length() - 1] = ']';
		res_str += "}";
		return res_str;
	}

	AsyncConnection::AsyncConnection(const connection_options& options) {
		connect_string = "dbname = " + options.dbname + " user = " + options.user + " password = " + options.password + " hostaddr = " + options.hostaddr + " port = " + options.port;
		conn = std::make_shared<pqxx::connection>(connect_string);
	}
	AsyncConnection::AsyncConnection(const AsyncConnection &db) {
		connect_string = db.connect_string;
		conn = db.conn;
	}
	pqxx::result AsyncConnection::query(const std::string &sql) {
		if (!conn -> is_open()) {
			conn = std::make_shared<pqxx::connection>(connect_string);
		}
		pqxx::work w(*conn);
		pqxx::result r = w.exec(sql);
		return r;
	}
	void AsyncConnection::prepare(const std::string &name, const std::string &sql) {
		if (!conn -> is_open()) {
			conn = std::make_shared<pqxx::connection>(connect_string);
		}
		conn -> prepare(name, sql);
	}
	pqxx::result AsyncConnection::execute(const std::string &name) {
		if (!conn -> is_open()) {
			conn = std::make_shared<pqxx::connection>(connect_string);
		}
		pqxx::work w(*conn);
		pqxx::result r = w.exec_prepared(name);
		return r;
	}

	ConnectionsManager::ConnectionsManager(const connection_options &options, int numberOfConnections) : options(options), numberOfConnections(numberOfConnections) {}

	ConnectionsManager::ConnectionsManager() {}

	ConnectionsManager::~ConnectionsManager() {
		disconnect();
	}

	void ConnectionsManager::connect() {
		for (int i = 0; i < numberOfConnections; i++) {
			std::unique_ptr<AsyncConnection> db_ptr = std::make_unique<AsyncConnection>(options);
			connections.push(std::move(db_ptr));
		}
	}

	void ConnectionsManager::disconnect() {
		for(int i = 0; i < numberOfConnections; i++) {
			std::unique_ptr<AsyncConnection> db_ptr = std::move(connections.front());
			connections.pop();
			db_ptr -> conn -> disconnect();
		}
	}

	std::unique_ptr<AsyncConnection> ConnectionsManager::getDatabase() {
		std::unique_lock<std::mutex> lock(mtx);
		cv.wait(lock, [this] { return !connections.empty(); });
		if(!connections.empty()) {
			// std::cout << "Database available" << std::endl;
			std::unique_ptr<AsyncConnection> db_ptr = std::move(connections.front());
			connections.pop();
			lock.unlock();
			cv.notify_one();
			return db_ptr;
		}
		if(connections.empty())
			throw std::runtime_error("queue empty");
		throw std::runtime_error("No database available");
	}

	void ConnectionsManager::returnDatabase(std::unique_ptr<AsyncConnection> db_ptr) {
		{
			std::lock_guard<std::mutex> lock(mtx);
			connections.push(std::move(db_ptr));
		}
		cv.notify_one();
	}


}
