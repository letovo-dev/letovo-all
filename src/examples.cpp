#include <iostream>
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>
#include <fstream>
#include <chrono>
#include <string>
#include <ctime>
#include <pqxx/pqxx>
#include "pqxx_cp.h"
#include "rapidjson/document.h" 
#include "spdlog/spdlog.h"


std::ifstream in("image.png", std::ios::in | std::ios::binary);
std::ostringstream contents;
std::ofstream log_ofstream;

static std::string timePointAsString(const std::chrono::system_clock::time_point& tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::string ts = std::ctime(&t);
    ts.resize(ts.size()-1);
    return ts;
}

void server_example() {    
    // std::ostringstream contents;
    contents << in.rdbuf();
    in.close();
    httplib::Server svr;
    svr.Get("/hi", [](const httplib::Request &req, httplib::Response &res) {
        log_ofstream.open("./log.txt");

        auto time = std::chrono::system_clock::now();
        if(log_ofstream)
            log_ofstream << req.remote_addr << ' ' << timePointAsString(time) << '\n';
        res.set_content(contents.str(), "image/png");
        log_ofstream.close();
    });

    svr.listen("0.0.0.0", 8080);

}

void postgresql_example() {
    pqxx::connection con("user=<username> password=<passwd (default 1234)> host=91.203.232.173 port=5432 dbname=sandbox target_session_attrs=read-write");
    pqxx::work transaction(con);
    pqxx::result rows = transaction.exec("SELECT * FROM Users;");
    for (int i = 0; i < rows.size(); i++) {
        auto [name, user_id] = rows[i].as<std::string, int>();
        std::cout << name << ", " << user_id << std::endl;
    }
}

void connection_pool() {
    std::ifstream config_file("SqlConnectionConfig.json");
    std::string json((std::istreambuf_iterator<char>(config_file)), std::istreambuf_iterator<char>());

    rapidjson::Document sql_config;

    sql_config.Parse(json.c_str());
    
    cp::connection_options options;

    options.user = sql_config["user"].GetString();
    options.password = sql_config["password"].GetString();
    options.dbname = sql_config["dbname"].GetString();
    options.hostaddr = sql_config["host"].GetString();
    
    cp::connection_pool pool{options};

    cp::query create_table("SELECT * FROM User;");

    auto tx = cp::tx(pool, create_table);

    pqxx::result r = create_table();

    tx.commit();

    std::string s1 = r[0][0].as<std::string>();

    std::cout<<'\n'<<s1<<'\n';
} 

void loger() 
{
    spdlog::info("Welcome to spdlog!");
    spdlog::error("Some error message with arg: {}", 1);
    
    spdlog::warn("Easy padding in numbers like {:08d}", 12);
    spdlog::critical("Support for int: {0:d};  hex: {0:x};  oct: {0:o}; bin: {0:b}", 42);
    spdlog::info("Support for floats {:03.2f}", 1.23456);
    spdlog::info("Positional args are {1} {0}..", "too", "supported");
    spdlog::info("{:<30}", "left aligned");
    
    spdlog::set_level(spdlog::level::debug); // Set global log level to debug
    spdlog::debug("This message should be displayed..");    
    
    // change log pattern
    spdlog::set_pattern("[%H:%M:%S %z] [%n] [%^---%L---%$] [thread %t] %v");
    
    // Compile time log levels
    // Note that this does not change the current log level, it will only
    // remove (depending on SPDLOG_ACTIVE_LEVEL) the call on the release code.
    SPDLOG_TRACE("Some trace message with param {}", 42);
    SPDLOG_DEBUG("Some debug message");
}


int main() {
}