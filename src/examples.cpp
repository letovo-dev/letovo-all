#include <iostream>
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>
#include <fstream>
#include <chrono>
#include <string>
#include <ctime>
#include <pqxx/pqxx>
#include "pqxx_cp.cc"
#include "rapidjson/document.h" 


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


int main() {

}