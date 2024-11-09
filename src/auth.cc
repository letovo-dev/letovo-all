#include "auth.h"

#include <iostream>

namespace auth {    
    bool is_authed(std::string token, std::shared_ptr<cp::connection_pool> pool_ptr) { 
        auto decoded = hashing::string_from_hash(token);

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

    bool is_admin(std::string token, std::shared_ptr<cp::connection_pool> pool_ptr) {
        auto decoded = hashing::string_from_hash(token);

        cp::query get_user("SELECT * FROM \"user\" WHERE \"username\"=($1) AND \"userrights\"=($2);");

        auto tx = cp::tx(*pool_ptr, get_user);
        
        pqxx::result result = get_user(decoded, "admin");

        if(result.empty()) {
            return false;
        }
        return true;
    }

    void enable_auth(std::unique_ptr<restinio::router::express_router_t<>>& router, 
                    std::shared_ptr<cp::connection_pool> pool_ptr, 
                    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_post("/auth/login", [pool_ptr, logger_ptr](auto req, auto) {
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
                    auto token = hashing::hash_from_string(loginHeader);
                    
                    return req->create_response().set_body("{\"token\": \"" + token + "\"}").done();
                }
            }
            
            else {
                return req->create_response(restinio::status_non_authoritative_information()) .append_header_date_field().connection_close().done();
            }

        }); 
    }

    void enable_reg(std::unique_ptr<restinio::router::express_router_t<>>& router, 
                        std::shared_ptr<cp::connection_pool> pool_ptr, 
                        std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_post("/auth/reg", [pool_ptr, logger_ptr](auto req, auto) {
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

                    auto token = hashing::hash_from_string(loginHeader); 
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
    }

    void am_i_authed(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/auth/amiauthed/:token([a-zA-Z0-9]+))", [pool_ptr](auto req, auto){
            std::string token = url::get_last_url_arg(req->header().path());

            if(token.empty()) {
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }
            if(auth::is_authed(token, pool_ptr)) {
                return req->create_response(restinio::status_ok()).done();
            } else {
                return req->create_response(restinio::status_unauthorized()).set_body("no").done();
            }
        });
    }

    void am_i_admin(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/auth/amiadmin/:token([a-zA-Z0-9]+))", [pool_ptr](auto req, auto){
            std::string token = url::get_last_url_arg(req->header().path());

            if(token.empty()) {
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }
            if(auth::is_admin(token, pool_ptr)) {
                return req->create_response(restinio::status_ok()).done();
            } else {
                return req->create_response(restinio::status_unauthorized()).set_body("no").done();
            }
        });
    }

    void enable_delete(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_delete(R"(/auth/delete/:token([a-zA-Z0-9]+))", [pool_ptr, logger_ptr](auto req, auto){
            std::string token = url::get_last_url_arg(req->header().path());

            if(token.empty()) {
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }

            std::string username = hashing::string_from_hash(token);

            if(auth::is_authed(token, pool_ptr)) {
                try {
                    cp::query delete_user("DELETE FROM \"user\" WHERE \"username\"=($1);");
                    auto tx = cp::tx(*pool_ptr, delete_user);
                    delete_user(username);
                    tx.commit();
                } catch(const char* error_message) {logger_ptr->error( [username, error_message]{return fmt::format("error on delete user from {} {}", username, error_message);});}
                
                logger_ptr->info( [username]{return fmt::format("user {} deleted", username);});
                return req->create_response().set_body("ok").done();
            } else {
                return req->create_response(restinio::status_unauthorized()).done();
            }
        });

    }

    void add_userrights(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_put(R"(/auth/add_userrights/:token([a-zA-Z0-9]+))", [pool_ptr, logger_ptr](auto req, auto){
            std::string token = url::get_last_url_arg(req->header().path());

            if(token.empty()) {
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }

            std::string username = hashing::string_from_hash(token);

            if(!auth::is_admin(token, pool_ptr)) {
                return req->create_response(restinio::status_unauthorized()).set_body("ты даже не гражданин!").done();
            }
            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());

            if (new_body.HasMember("username") && new_body.HasMember("rights")) {
                std::string user = new_body["username"].GetString();
                std::string rights = new_body["rights"].GetString();

                cp::query add_userrights("UPDATE \"user\" SET \"userrights\"=($1) WHERE \"username\"=($2);");
                auto tx = cp::tx(*pool_ptr, add_userrights);
                add_userrights(rights, user);
                tx.commit();
                logger_ptr->info( [username, user, rights]{return fmt::format("user {} added rights {} to {}", username, rights, user);});
                return req->create_response().set_body("ok").done();
            }
            else {
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }
        });
    }


}