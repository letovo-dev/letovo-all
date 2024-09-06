#include <pqxx/pqxx>
#include <httplib.h>

void enable_auth(std::shared_ptr<httplib::Server> p) {
    p->Get("/hi", [](const httplib::Request &req, httplib::Response &res) {
        res.set_content("content", "image/png");
    });
    // svr.Get("/hi", [](const httplib::Request &req, httplib::Response &res) {
    //     res.set_content("content", "image/png");
    // });
}
 

int main() {
    std::shared_ptr<httplib::Server> p = std::make_shared<httplib::Server>();;

}