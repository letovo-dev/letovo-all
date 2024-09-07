#include <pqxx/pqxx>
#include <httplib.h>
#include "letovo-soc-net/chats.cc"

void enable_auth(std::shared_ptr<httplib::Server> p) {
    p->Get("/hi", [](const httplib::Request &req, httplib::Response &res) {
        res.set_content("Hello World!", "text/plain");
    });
}


int main() {
    std::shared_ptr<httplib::Server> p = std::make_shared<httplib::Server>();

    enable_auth(p);
    placeholder(p);

    p -> listen("0.0.0.0", 8080);
}