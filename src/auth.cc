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

                // TODO: wrong status, change
                return req->create_response(restinio::status_non_authoritative_information()) .append_header_date_field().connection_close().done();
                
            }
            else {
                auto token = jwt::create()
                    .set_type("JWS")
                    .set_issuer("auth0")
                    .set_id(loginHeader)
                    .sign(jwt::algorithm::hs256{"secret"});
                logger_ptr->info( [endpoint]{return fmt::format("{} is authed", endpoint);});
                
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

                auto token = jwt::create()
                    .set_type("JWS")
                    .set_issuer("auth0")
                    .set_id(loginHeader)
                    .sign(jwt::algorithm::hs256{"secret"});  
            }
            catch(const char* error_message) {std::cout<<error_message<<std::endl;} 
            logger_ptr->info( [endpoint, loginHeader]{return fmt::format("new user with ip {} username {}", endpoint, loginHeader);});
            
            return req->create_response().set_body("ok").done();
        }
        else
        {
            return req->create_response(restinio::status_non_authoritative_information()).done();
        }
    });

    // I NEED TO BE GET WITH HEADERS
    router.get()->http_get("/amiauthed", [pool_ptr](auto req, auto){
        rapidjson::Document new_body;
        new_body.Parse(req->body().c_str());
        
        if(new_body.HasMember("token")) {
            if(is_authed(new_body["token"].GetString(), pool_ptr)) {
                return req->create_response(restinio::status_ok()).done();
            } else {
                return req->create_response(restinio::status_bad_request()).done();
            }
        } else {
            return req->create_response(restinio::status_non_authoritative_information()).done();
        }
    });

}