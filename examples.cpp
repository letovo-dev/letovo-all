#include <iostream>
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>
#include <fstream>
#include <chrono>
#include <string>
#include <ctime>
#include <pqxx/pqxx>

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


int main() {

}