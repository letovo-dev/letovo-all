#include <iostream>
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>
#include <fstream>
#include <chrono>
#include <string>
#include <ctime>
#include <pqxx/pqxx>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>

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

// This message handler will be invoked once for each incoming message. It
// prints the message and then sends a copy of the message back to the server.
void on_message(websocketpp::client<websocketpp::config::asio_client> * c, 
        websocketpp::connection_hdl hdl, 
        websocketpp::config::asio_client::message_type::ptr msg
        ) {
    std::cout << "on_message called with hdl: " << hdl.lock().get()
              << " and message: " << msg->get_payload()
              << std::endl;


    websocketpp::lib::error_code ec;

    c->send(hdl, msg->get_payload(), msg->get_opcode(), ec);
    if (ec) {
        std::cout << "Echo failed because: " << ec.message() << std::endl;
    }
}

using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;

int websocket_example(int argc, char* argv[]) {
// Create a client endpoint
    websocketpp::client<websocketpp::config::asio_client>  c;
    std::string uri = "ws://localhost:9002";

    if (argc == 2) {
        uri = argv[1];
    }

    try {
        // Set logging to be pretty verbose (everything except message payloads)
        c.set_access_channels(websocketpp::log::alevel::all);
        c.clear_access_channels(websocketpp::log::alevel::frame_payload);

        // Initialize ASIO
        c.init_asio();

        // Register our message handler
        c.set_message_handler(bind(&on_message,&c,::_1,::_2));

        websocketpp::lib::error_code ec;
        websocketpp::client<websocketpp::config::asio_client>::connection_ptr con = c.get_connection(uri, ec);
        if (ec) {
            std::cout << "could not create connection because: " << ec.message() << std::endl;
            return 0;
        }

        // Note that connect here only requests a connection. No network messages are
        // exchanged until the event loop starts running in the next line.
        c.connect(con);

        // Start the ASIO io_service run loop
        // this will cause a single connection to be made to the server. c.run()
        // will exit when this connection is closed.
        c.run();
    } catch (websocketpp::exception const & e) {
        std::cout << e.what() << std::endl;
    }
    return 1;
}

int main() {

}