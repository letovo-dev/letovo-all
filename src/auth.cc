#include <jwt-cpp/jwt.h>
#include <httplib.h>
#include "pqxx_cp.cc"

void auth(std::shared_ptr<httplib::Server> svr_ptr, cp::connection_pool& pool) {
    svr_ptr->Get("/auth", [](const httplib::Request& req, httplib::Response& res)
    {
        int status = 200;

        auto loginHeader = req.get_header_value("login");
        auto passwordHeader = req.get_header_value("password");

        if (!loginHeader.empty() && !passwordHeader.empty())
        {
            // get password from db, compare hash
        }
        else
        {
            status = 403;
        }

        auto token = jwt::create()
            .set_type("JWS")
            .set_issuer("auth0")
            .set_id(loginHeader)
            .sign(jwt::algorithm::hs256{"secret"});

        res.status = status;
        res.set_header("token", token);

        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
    });

    svr_ptr->Get("/reg", [](const httplib::Request& req, httplib::Response& res)
    {
        int status = 200;

        auto loginHeader = req.get_header_value("login");
        auto passwordHeader = req.get_header_value("password");

        if (!loginHeader.empty() && !passwordHeader.empty())
        {
            // create password hash, write info to db
        }
        else
        {
            status = 403;
        }

        auto token = jwt::create()
            .set_type("JWS")
            .set_issuer("auth0")
            .set_id(loginHeader)
            .sign(jwt::algorithm::hs256{"secret"});


        res.status = status;
        res.set_header("token", token);

        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
    });
}


bool is_authed(std::string token, cp::connection_pool& pool) { 
    auto decoded = jwt::decode(token);
    if (decoded.has_id())
        auto user = decoded.get_id();
    else return false;

    // if user in db 
    return true;
    // else
    return false;
}

bool is_authed(const httplib::Request& req, cp::connection_pool& pool) {
    auto token = req.get_header_value("token");
    return is_authed(token, pool);
}

// int main() {
//     auto token = jwt::create()
//         .set_type("JWS")
//         .set_issuer("auth0")
//         .set_id("scv")
//         .set_payload_claim("user", jwt::claim(std::string("scv")))
//         .sign(jwt::algorithm::hs256{"secret"});

//     std::cout<<token<<std::endl;

//     auto decoded = jwt::decode(token);  

//     for(auto& e : decoded.get_payload_json())
//         std::cout << e.first << " = " << e.second << std::endl;

//     std::cout<<std::endl;   
// }