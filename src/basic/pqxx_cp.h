#pragma once

#include <chrono>
#include <curl/curl.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <queue>
#include <mutex>
#include <vector>
#include <pqxx/pqxx>
// #include <format>
#include <condition_variable>
#include <string>

namespace cp
{
	std::string serialize(pqxx::result res);
	std::string serialize(pqxx::row res);

	struct connection_options
	{
		std::string dbname{};
		std::string user{};
		std::string password{};
		std::string hostaddr{};
		std::string port = "5432";

		int connections_count = 8;
	};
	class AsyncConnection {
		public:
			std::shared_ptr<pqxx::connection> con;
			std::string connect_string;
			bool free;
			std::string name;
			AsyncConnection(const connection_options& options, std::string name);
			AsyncConnection(const AsyncConnection &db);
			pqxx::result query(const std::string &sql);
			void prepare(const std::string &sql);
			void prepare(const std::string &_name, const std::string &sql);
			pqxx::result execute_params(const std::string &sql, std::vector<std::string> &params, bool commit = false);
			pqxx::result execute_params(const std::string &sql, std::vector<int> &params, bool commit = false);
			pqxx::result execute_prepared(int &&args);
			pqxx::result execute_prepared(const std::string &&args);
			pqxx::result execute_prepared(const std::string &_name, int &&args);
			pqxx::result execute_prepared(const std::string &_name, std::basic_string<char> &args);
			pqxx::result execute(const std::string &sql, bool commit = false);
	};
	class ConnectionsManager {
    public:
		connection_options options;
		int numberOfConnections;
		std::queue<std::unique_ptr<AsyncConnection>> connections;
		std::thread worker;
		std::mutex mtx;
		std::condition_variable cv;

        ConnectionsManager(const connection_options &options, int numberOfConnections);
		ConnectionsManager();
		void connect();
		std::unique_ptr<AsyncConnection> getConnection();
		void returnConnection(std::unique_ptr<AsyncConnection> db_ptr);
	};
	
	
}
