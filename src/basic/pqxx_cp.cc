#include "pqxx_cp.h"

namespace cp {
    std::string serialize(const pqxx::result &res) {
        // TODO: it can be better
        if (res.empty()) {
            return "{\"result\": []}";
        }
        std::string res_str = "{\"result\": [";
        for (auto const& row : res) {
            res_str += cp::serialize(row);
            res_str += ',';
        }
        res_str[res_str.length() - 1] = ']';
        res_str += "}";
        return res_str;
    }

    std::string serialize(const pqxx::row &row) {
        // TODO: it can be better
        if (row.empty()) {
            return "{}";
        }
        std::string res_str = "{";
        for (auto const& field : row) {
            res_str += '"' + std::string(field.name()) + "\": \"" + std::string(field.c_str()) + "\",";
        }

        res_str[res_str.length() - 1] = '}';
        return res_str;
    }

    std::string serialize(const std::vector<std::string> &vec) {
        if(vec.empty()) {
            return "{}";
        }
        std::string res_str = "{\"result\": [";
        for (auto const& field : vec) {
            res_str += '"' + field + "\",";
        }
        res_str[res_str.length() - 1] = ']';
        res_str += "}";
        return res_str;
    }

    AsyncConnection::AsyncConnection(const connection_options& options, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr, std::string name)
        : name(name)
        , logger_ptr_(logger_ptr)
    {
        connect_string = "dbname = " + options.dbname + " user = " + options.user + " password = " + options.password + " hostaddr = " + options.hostaddr + " port = " + options.port;
        con = std::make_shared<pqxx::connection>(connect_string);
        last_used = std::chrono::system_clock::now();
    }
    AsyncConnection::AsyncConnection(const AsyncConnection& db) {
        connect_string = db.connect_string;
        con = db.con;
        name = db.name;
        last_used = std::chrono::system_clock::now();
        logger_ptr_ = db.logger_ptr_;
    }
    pqxx::result AsyncConnection::query(const std::string& sql) {
        if (!is_open()) {
            con = std::make_shared<pqxx::connection>(connect_string);
        }
        pqxx::work w(*con);
        pqxx::result r = w.exec(sql);
        last_used = std::chrono::system_clock::now();
        return r;
    }
    void AsyncConnection::prepare(const std::string& sql) {
        if (!is_open()) {
            con = std::make_shared<pqxx::connection>(connect_string);
        }
        con->prepare(name, sql);
        last_used = std::chrono::system_clock::now();
    }

    void AsyncConnection::prepare(const std::string& _name, const std::string& sql) {
        if (!is_open()) {
            con = std::make_shared<pqxx::connection>(connect_string);
        }
        con->prepare(_name, sql);
        last_used = std::chrono::system_clock::now();
    }

    pqxx::result AsyncConnection::execute_prepared(int&& args) {
        if (!is_open()) {
            con = std::make_shared<pqxx::connection>(connect_string);
        }
        pqxx::work w(*con);
        pqxx::result r = w.exec_prepared(name, args);
        last_used = std::chrono::system_clock::now();
        return r;
    }

    pqxx::result AsyncConnection::execute_prepared(const std::string&& args) {
        if (!is_open()) {
            con = std::make_shared<pqxx::connection>(connect_string);
        }
        pqxx::work w(*con);
        pqxx::result r = w.exec_prepared(name, args);
        last_used = std::chrono::system_clock::now();
        return r;
    }

    pqxx::result AsyncConnection::execute_prepared(const std::string& _name, int&& args) {
        if (!is_open()) {
            con = std::make_shared<pqxx::connection>(connect_string);
        }
        pqxx::work w(*con);
        pqxx::result r = w.exec_prepared(name, args);
        last_used = std::chrono::system_clock::now();
        return r;
    }

    pqxx::result AsyncConnection::execute_prepared(const std::string& _name, std::basic_string<char>& args) {
        if (!is_open()) {
            con = std::make_shared<pqxx::connection>(connect_string);
        }
        pqxx::work w(*con);
        pqxx::result r = w.exec_prepared(name, args);
        last_used = std::chrono::system_clock::now();
        return r;
    }

    pqxx::result AsyncConnection::execute_params(const std::string& sql, std::vector<std::string>& params, bool commit) {
        if (!is_open()) {
            con = std::make_shared<pqxx::connection>(connect_string);
        }
        pqxx::work w(*con); pqxx::result r;
        try {
            r = w.exec_params(sql, pqxx::prepare::make_dynamic_params(params));
        } catch (const std::exception& e) {
            logger_ptr_->error([e, sql] { return fmt::format("{} in {}", e.what(), sql); });
        }
        if (commit) {
            w.commit();
        }
        last_used = std::chrono::system_clock::now();
        return r;
    }

    pqxx::result AsyncConnection::execute_params(const std::string& sql, std::vector<int>& params, bool commit) {
        if (!is_open()) {
            con = std::make_shared<pqxx::connection>(connect_string);
        }
        pqxx::work w(*con); pqxx::result r;
        try {
            r = w.exec_params(sql, pqxx::prepare::make_dynamic_params(params));
        } catch (const std::exception& e) {
            logger_ptr_->error([e, sql] { return fmt::format("Error: {} in {}", e.what(), sql); });
        }
        if (commit) {
            w.commit();
        }
        last_used = std::chrono::system_clock::now();
        return r;
    }

    pqxx::result AsyncConnection::execute_params(Request req) {
        if (!is_open()) {
            con = std::make_shared<pqxx::connection>(connect_string);
        }
        pqxx::work w(*con); pqxx::result r;
        try {
            r = w.exec_params(req.sql, pqxx::prepare::make_dynamic_params(req.params));
        } catch (const std::exception& e) {
            logger_ptr_->error([e, req] { return fmt::format("Error: {} in {}", e.what(), req.sql); });
        }
        if (req.commit) {
            w.commit();
        }
        last_used = std::chrono::system_clock::now();
        return r;
    }

    pqxx::result AsyncConnection::execute_many(std::vector<Request>& reqs) {
        if (!is_open()) {
            con = std::make_shared<pqxx::connection>(connect_string);
        }
        bool commit = false;
        pqxx::work w(*con); pqxx::result r;
        for (auto& req : reqs) {
            try {
                r = w.exec_params(req.sql, pqxx::prepare::make_dynamic_params(req.params));
            } catch (const std::exception& e) {
                logger_ptr_->error([e, req] { return fmt::format("Error: {} in {}", e.what(), req.sql); });
            }
            if (req.commit) {
                commit = true;
            }
        }
        if (commit)
            w.commit();
        last_used = std::chrono::system_clock::now();
        return r;
    }

    pqxx::result AsyncConnection::execute(const std::string& sql, bool commit) {
        if (!is_open()) {
            con = std::make_shared<pqxx::connection>(connect_string);
        }
        pqxx::work w(*con); pqxx::result r;
        try {
            r = w.exec(sql);
        } catch (const std::exception& e) {
            logger_ptr_->error([e, sql] { return fmt::format("Error: {} in {}", e.what(), sql); });
        }
        if (commit) {
            w.commit();
        }
        last_used = std::chrono::system_clock::now();
        return r;
    }

    bool AsyncConnection::is_open() {
        return last_used + std::chrono::seconds(60) > std::chrono::system_clock::now();
    }

    ConnectionsManager::ConnectionsManager(std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr, const connection_options& options)
        : options(options)
        , logger_ptr_(logger_ptr)
        , numberOfConnections(options.connections_count)
    {
    }

    ConnectionsManager::ConnectionsManager(std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr, const connection_options& options, int numberOfConnections)
        : options(options)
        , logger_ptr_(logger_ptr)
        , numberOfConnections(numberOfConnections)
    {
    }

    ConnectionsManager::ConnectionsManager() {
    }

    void ConnectionsManager::connect() {
        for (int i = 0; i < numberOfConnections; i++) {
            std::unique_ptr<AsyncConnection> db_ptr = std::make_unique<AsyncConnection>(options, logger_ptr_, std::to_string(i));
            connections.push(std::move(db_ptr));
        }
    }

    std::unique_ptr<AsyncConnection> ConnectionsManager::getConnection() {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [this] { return !connections.empty(); });
        if (!connections.empty()) {
            // std::cout << "Database available" << std::endl;
            std::unique_ptr<AsyncConnection> db_ptr = std::move(connections.front());
            connections.pop();
            lock.unlock();
            cv.notify_one();
            return db_ptr;
        }
        if (connections.empty()) {
            throw std::runtime_error("queue empty");
        }
        throw std::runtime_error("No database available");
    }

    void ConnectionsManager::returnConnection(std::unique_ptr<AsyncConnection> db_ptr) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            connections.push(std::move(db_ptr));
        }
        cv.notify_one();
    }

} // namespace cp
