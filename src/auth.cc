#include <jwt-cpp/jwt.h>
#include <httplib.h>
#include "pqxx_cp.cc"
#include <pqxx/pqxx>
#include <unordered_set> 

void auth(std::shared_ptr<httplib::Server> svr_ptr, cp::connection_pool pool) {
    svr_ptr->Get("/auth", [&](const httplib::Request& req, httplib::Response& res)
    {
        int status = 200;

        std::string loginHeader = req.get_header_value("login");
        std::string passwordHeader = req.get_header_value("password");

        if (loginHeader.empty() || passwordHeader.empty())
        {
            status = 403;
        }
        else
        {
            // get password from db, compare hash
            std::string passwordHash = std::to_string(std::hash<std::string>{}(passwordHeader));

            cp::query get_user("SELECT userId User WHEREE userName=($1) AND passwdHash=($2)");
            auto tx = cp::tx(pool, get_user);
            
            pqxx::result result = get_user(loginHeader, passwordHash);
            if(result.empty()) {
                status = 401;
            }
            else {
                auto token = jwt::create()
                    .set_type("JWS")
                    .set_issuer("auth0")
                    .set_id(loginHeader)
                    .sign(jwt::algorithm::hs256{"secret"});

                res.set_header("token", token);
            }
        }
        res.status = status;

    });

    svr_ptr->Get("/reg", [&](const httplib::Request& req, httplib::Response& res)
    {
        int status = 200;

        auto loginHeader = req.get_header_value("login");
        auto passwordHeader = req.get_header_value("password");

        if (!loginHeader.empty() && !passwordHeader.empty())
        {
            // create password hash, write info to db
            std::string passwordHash = std::to_string(std::hash<std::string>{}(passwordHeader));

            cp::query add_user("INSERT INTO public.\"user\" (userid, username, passwdhash, userrights, jointime) VALUES($1, '$2', '$3', '', now());");

            auto tx = cp::tx(pool, add_user);

            pqxx::result result = add_user(std::stoi(passwordHash), loginHeader, passwordHash);         

            auto token = jwt::create()
                .set_type("JWS")
                .set_issuer("auth0")
                .set_id(loginHeader)
                .sign(jwt::algorithm::hs256{"secret"});  
        }
        else
        {
            status = 403;
        }
        res.status = status;
    });
}


bool is_authed(std::string token, cp::connection_pool& pool) { 
    auto decoded = jwt::decode(token);
    std::string user;
    if (decoded.has_id())
        user = decoded.get_id();
    else return false;

    // if user in db true, else false 
    cp::query get_user("SELECT userId User WHEREE userName=($1);");
    auto tx = cp::tx(pool, get_user);
    
    pqxx::result result = get_user(user);

    if(result.empty()) {
        return false;
    }
    return true;
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