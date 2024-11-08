#include "auth.h"

#include <iostream>

bool is_authed(std::string token, std::shared_ptr<cp::connection_pool> pool_ptr) { 
    auto decoded = string_from_hash(token);

    // if user in db true, else false 
    cp::query get_user("SELECT * FROM \"user\" WHERE \"username\"=($1);");

    auto tx = cp::tx(*pool_ptr, get_user);
    
    pqxx::result result = get_user(decoded);

    if(result.empty()) {
        return false;
    }
    return true;
}

bool is_authed_by_body(std::string req_body, std::shared_ptr<cp::connection_pool> pool_ptr) {
    rapidjson::Document new_body;
    new_body.Parse(req_body.c_str());

    if (new_body.HasMember("token")) {
        std::string token = new_body["token"].GetString();
        return is_authed(token, pool_ptr);
    }
    return false;
}


void enable_auth_reg(std::unique_ptr<restinio::router::express_router_t<>>& router, 
                    std::shared_ptr<cp::connection_pool> pool_ptr, 
                    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
    router.get()->http_post("/auth", [pool_ptr, logger_ptr](auto req, auto) {
        std::string endpoint = req->remote_endpoint().address().to_string();
        logger_ptr->info( [endpoint]{return fmt::format("auth request from {}", endpoint);});

        rapidjson::Document new_body;
        new_body.Parse(req->body().c_str());

        if (new_body.HasMember("login") && new_body.HasMember("password")) {
            std::string loginHeader = new_body["login"].GetString();
            std::string passwordHeader = new_body["password"].GetString();

            // get password from db, compare hash
            std::string passwordHash = std::to_string(std::hash<std::string>{}(passwordHeader));

            cp::query get_user("SELECT * FROM \"user\" WHERE \"username\"=($1) AND \"passwdhash\"=($2);");
            auto tx = cp::tx(*pool_ptr, get_user);
            pqxx::result result = get_user(loginHeader, passwordHash);
            
            if(result.empty()) {
                logger_ptr->info( [endpoint]{return fmt::format("empty headers from {}", endpoint);});

                return req->create_response(restinio::status_unauthorized()) .append_header_date_field().connection_close().done();
                
            }
            else {
                auto token = hash_from_string(loginHeader);
                
                return req->create_response().set_body("{\"token\": \"" + token + "\"}").done();
            }
        }
        
        else {
            return req->create_response(restinio::status_non_authoritative_information()) .append_header_date_field().connection_close().done();
        }

    }); 


    router.get()->http_post("/reg", [pool_ptr, logger_ptr](auto req, auto) {
        std::string endpoint = req->remote_endpoint().address().to_string();

        logger_ptr->info( [endpoint]{return fmt::format("reg request from {}", endpoint);});


        rapidjson::Document new_body;
        new_body.Parse(req->body().c_str());

        if (new_body.HasMember("login") && new_body.HasMember("password"))
        {
            std::string loginHeader = new_body["login"].GetString();
            std::string passwordHeader = new_body["password"].GetString();

            try {
                // create password hash, write info to db
                std::string passwordHash = std::to_string(std::hash<std::string>{}(passwordHeader));

                cp::query add_user("INSERT INTO \"user\" (userid, username, passwdhash, userrights, jointime) VALUES($1, $2, $3, '', now());");

                auto tx = cp::tx(*pool_ptr, add_user);

                pqxx::result result = add_user(std::stoul(passwordHash) % 10000, loginHeader, passwordHash);      

                tx.commit();

                auto token = hash_from_string(loginHeader); 
            }
            catch(const char* error_message) {logger_ptr->error( [endpoint, error_message]{return fmt::format("error from {} {}", endpoint, error_message);});}
            logger_ptr->info( [endpoint, loginHeader]{return fmt::format("new user with ip {} username {}", endpoint, loginHeader);});
            
            return req->create_response().set_body("ok").done();
        }
        else
        {
            return req->create_response(restinio::status_non_authoritative_information()).done();
        }
    });
    
    router.get()->http_get(R"(/amiauthed/:token([a-zA-Z0-9]+))", [pool_ptr](auto req, auto){
        std::string token = get_url_arg(req->header().path());

        if(token.empty()) {
            return req->create_response(restinio::status_non_authoritative_information()).done();
        }
        if(is_authed(token, pool_ptr)) {
            return req->create_response(restinio::status_ok()).done();
        } else {
            return req->create_response(restinio::status_unauthorized()).done();
        }
    });

}