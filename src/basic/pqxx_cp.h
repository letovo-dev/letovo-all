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
			std::shared_ptr<pqxx::connection> conn;
			std::string connect_string;
			bool free;
			AsyncConnection(const connection_options& options);
			AsyncConnection(const AsyncConnection &db);
			pqxx::result query(const std::string &sql);
			void prepare(const std::string &name, const std::string &sql);
			pqxx::result execute(const std::string &name);
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
		~ConnectionsManager();
		void connect();
		void disconnect();
		std::unique_ptr<AsyncConnection> getDatabase();
		void returnDatabase(std::unique_ptr<AsyncConnection> db_ptr);
	};
	
	
}
