#pragma once

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <queue>
#include <mutex>
#include <vector>
#include <pqxx/pqxx>
#include <chrono>
#include <condition_variable>
#include <string>

namespace cp {
    std::string serialize(const pqxx::result &res);
    std::string serialize(const pqxx::row &res);
    std::string serialize(const std::vector<std::string> &vec);

    struct connection_options {
        std::string dbname{};
        std::string user{};
        std::string password{};
        std::string hostaddr{};
        std::string port = "5432";

        int connections_count = 8;
    };
    class AsyncConnection {
    public:
        std::chrono::time_point<std::chrono::system_clock> last_used;
        AsyncConnection(const connection_options& options, std::string name);
        AsyncConnection(const AsyncConnection& db);
        pqxx::result query(const std::string& sql);
        void prepare(const std::string& sql);
        void prepare(const std::string& _name, const std::string& sql);
        pqxx::result execute_params(const std::string& sql, std::vector<std::string>& params, bool commit = false);
        pqxx::result execute_params(const std::string& sql, std::vector<int>& params, bool commit = false);
        pqxx::result execute_prepared(int&& args);
        pqxx::result execute_prepared(const std::string&& args);
        pqxx::result execute_prepared(const std::string& _name, int&& args);
        pqxx::result execute_prepared(const std::string& _name, std::basic_string<char>& args);
        pqxx::result execute(const std::string& sql, bool commit = false);
    private:
        std::shared_ptr<pqxx::connection> con;
        std::string connect_string;
        bool free;
        std::string name;
        bool is_open();
    };
    class ConnectionsManager {
    public:
        ConnectionsManager(const connection_options& options, int numberOfConnections);
        ConnectionsManager(const connection_options& options);
        ConnectionsManager();
        void connect();
        std::unique_ptr<AsyncConnection> getConnection();
        void returnConnection(std::unique_ptr<AsyncConnection> db_ptr);
    private:
        connection_options options;
        int numberOfConnections;
        std::queue<std::unique_ptr<AsyncConnection>> connections;
        std::thread worker;
        std::mutex mtx;
        std::condition_variable cv;
    };

} // namespace cp
