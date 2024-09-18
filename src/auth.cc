#include "auth.h"

#include <iostream>

bool is_authed(std::string token, std::shared_ptr<cp::connection_pool> pool_ptr) { 
    auto decoded = jwt::decode(token);
    std::string user;
    if (decoded.has_id())
        user = decoded.get_id();
    else return false;

    // if user in db true, else false 
    cp::query get_user("SELECT * FROM \"user\" WHERE \"username\"=($1);");

    auto tx = cp::tx(*pool_ptr, get_user);
    
    pqxx::result result = get_user(user);

    if(result.empty()) {
        return false;
    }
    return true;
}

bool is_authed(const httplib::Request& req, std::shared_ptr<cp::connection_pool> pool_ptr) {
    auto token = req.get_header_value("token");
    return is_authed(token, pool_ptr);
}

void enable_auth_reg(std::shared_ptr<httplib::Server> svr_ptr, std::shared_ptr<cp::connection_pool> pool_ptr) {
    svr_ptr->Get("/auth", [&](const httplib::Request& req, httplib::Response& res){
        spdlog::info("auth request from " + req.remote_addr);
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

            cp::query get_user("SELECT * FROM \"user\" WHERE \"username\"=($1) AND \"passwdhash\"=($2);");
            auto tx = cp::tx(*pool_ptr, get_user);
            pqxx::result result = get_user(loginHeader, passwordHash);
            
            if(result.empty()) {
                status = 401;
                spdlog::info("empty headers from " + req.remote_addr);
            }
            else {
                auto token = jwt::create()
                    .set_type("JWS")
                    .set_issuer("auth0")
                    .set_id(loginHeader)
                    .sign(jwt::algorithm::hs256{"secret"});

                res.set_header("token", token);
                res.set_content(token, "text/plain");
            }
        }
        spdlog::info(req.remote_addr + " is authed");
        res.status = status;
    });

    svr_ptr->Get("/reg", [&](const httplib::Request& req, httplib::Response& res){
        int status = 200;
        
        auto loginHeader = req.get_header_value("login");
        auto passwordHeader = req.get_header_value("password");

        if (!loginHeader.empty() && !passwordHeader.empty())
        {
            try {
                // create password hash, write info to db
                std::string passwordHash = std::to_string(std::hash<std::string>{}(passwordHeader));

                cp::query add_user("INSERT INTO \"user\" (userid, username, passwdhash, userrights, jointime) VALUES($1, $2, $3, '', now());");

                auto tx = cp::tx(*pool_ptr, add_user);

                pqxx::result result = add_user(std::stoul(passwordHash) % 10000, loginHeader, passwordHash);      

                tx.commit();

                auto token = jwt::create()
                    .set_type("JWS")
                    .set_issuer("auth0")
                    .set_id(loginHeader)
                    .sign(jwt::algorithm::hs256{"secret"});  
            }
            catch(const char* error_message) {std::cout<<error_message<<std::endl;} 
        }
        else
        {
            status = 403;
        }
        res.status = status;
        spdlog::info("new user " + loginHeader);
        res.set_content("ok", "text/plain");
    });

    svr_ptr -> Get("/amiauthed", [&](const httplib::Request& req, httplib::Response& res) {
        if(is_authed(req, pool_ptr)) {
            res.status = 200;
        } else {
            res.status = 403;
        }
    });
}

// DELETE ME
void test_http(cp::connection_pool& pool_ptr) {
    cp::query add_user("INSERT INTO public.\"user\" (userid, username, passwdhash, userrights, jointime) VALUES($1, $2, $3, '', now());");

    auto tx = cp::tx(pool_ptr, add_user);

    pqxx::result result = add_user(123, "scv2", "password2");

    tx.commit();
}