#include <iostream>
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>
#include <fstream>
#include <chrono>
#include <string>
#include <ctime>

void placeholder(std::shared_ptr<httplib::Server> svr_ptr) {
    svr_ptr -> Get("/hi2", [](const httplib::Request &req, httplib::Response &res) {
        
        res.set_content("placeholder", "placeholder 2 ");
    });
};
