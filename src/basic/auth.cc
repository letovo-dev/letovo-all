#include "./auth.h"

#include <iostream>

namespace auth {    
    std::string get_username(std::string token, std::shared_ptr<cp::connection_pool> pool_ptr) {
        auto decoded = hashing::string_from_hash(token);

        cp::query get_user("SELECT * FROM \"user\" WHERE \"username\"=($1);");

        auto tx = cp::tx(*pool_ptr, get_user);
        
        pqxx::result result = get_user(decoded);

        if(result.empty()) {
            return "";
        }
        return decoded;
    }

    bool is_authed(std::string token, std::shared_ptr<cp::connection_pool> pool_ptr) { 
        return get_username(token, pool_ptr) != "";
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

    bool is_user(std::string username, std::shared_ptr<cp::connection_pool> pool_ptr) {
        cp::query get_user("SELECT * FROM \"user\" WHERE \"username\"=($1);");

        auto tx = cp::tx(*pool_ptr, get_user);
        
        pqxx::result result = get_user(username);

        if(result.empty()) {
            return false;
        }
        return true;
    }

    bool is_admin_by_uname(std::string username, std::shared_ptr<cp::connection_pool> pool_ptr) {
        cp::query get_user("SELECT * FROM \"user\" WHERE \"username\"=($1) AND \"userrights\"=($2);");

        auto tx = cp::tx(*pool_ptr, get_user);
        
        pqxx::result result = get_user(username, "admin");

        if(result.empty()) {
            return false;
        }
        return true;
    }

    bool is_active(std::string username, std::shared_ptr<cp::connection_pool> pool_ptr) {
        cp::query get_user("SELECT active FROM \"user\" WHERE \"username\"=($1) and \"active\"=true;");

        auto tx = cp::tx(*pool_ptr, get_user);
        
        pqxx::result result = get_user(username);

        if(result.empty()) {
            return false;
        }
        return true;
    }

}

namespace auth::server {
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

            if (new_body.HasMember("login") && new_body.HasMember("password") /*&& new_body.HasMember("temp_token")*/)
            {
                std::string loginHeader = new_body["login"].GetString();
                std::string passwordHeader = new_body["password"].GetString();
                // TODO: discuss, may be better to reg new user each time by hands and then create qr code to change username and password
                // std::string temp_token = new_body["temp_token"].GetString();
                // if(!hashing::check_new_user(temp_token)) {
                //     return req->create_response(restinio::status_unauthorized()).done();
                // }

                try {
                    std::string passwordHash = std::to_string(std::hash<std::string>{}(passwordHeader));
                    std::string userid = std::to_string(std::hash<std::string>{}(loginHeader)).substr(0, 5);
                    cp::query add_user("INSERT INTO \"user\" (userid, username, passwdhash, userrights, jointime) VALUES($1, $2, $3, '', now());");

                    auto tx = cp::tx(*pool_ptr, add_user);
                    try {
                        pqxx::result result = add_user(userid, loginHeader, passwordHash);      
                    } catch (const pqxx::unique_violation& e) {
                        logger_ptr->warn( [endpoint, loginHeader, e]{return fmt::format("user {} already exists: {}", loginHeader, e.what());});
                        return req->create_response(restinio::status_forbidden()).set_body("username is ocupied").done();
                    }
                    tx.commit();

                    auto token = hashing::hash_from_string(loginHeader); 
                    logger_ptr->info( [endpoint, loginHeader]{return fmt::format("new user with ip {} username {}", endpoint, loginHeader);});
                
                    return req->create_response().set_body("{\"token\": \"" + token + "\"}").done();
                }
                catch(const char* error_message) {logger_ptr->error( [endpoint, error_message]{return fmt::format("error from {} {}", endpoint, error_message);});}
                
            }
            return req->create_response(restinio::status_non_authoritative_information()).done();
        });
    }

    void am_i_authed(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/auth/amiauthed/:token([a-zA-Z0-9]+))", [pool_ptr](auto req, auto) {
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
        router.get()->http_get(R"(/auth/amiadmin/:token([a-zA-Z0-9]+))", [pool_ptr](auto req, auto) {
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

    void is_user_active(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/auth/isactive/:username(.*))", [pool_ptr](auto req, auto) {
            std::string username = url::get_last_url_arg(req->header().path());

            if(auth::is_active(username, pool_ptr)) {
                return req->create_response(restinio::status_ok()).set_body("yes").done();
            } else {
                return req->create_response(restinio::status_ok()).set_body("no").done();
            }
        });
    }

    void is_user(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/auth/isuser/:username(.*))", [pool_ptr, logger_ptr](auto req, auto) {
            std::string username = url::get_last_url_arg(req->header().path());

            logger_ptr->info( [username]{return fmt::format("is user request for {}", username);});

            if(auth::is_user(username, pool_ptr)) {
                return req->create_response(restinio::status_ok()).set_body("yes").done();
            } else {
                return req->create_response(restinio::status_non_authoritative_information()).set_body("no").done();
            }
        });
    }

    void enable_delete(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_delete(R"(/auth/delete/:token([a-zA-Z0-9]+))", [pool_ptr, logger_ptr](auto req, auto) {
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
                    hashing::defele_from_hash(token);
                } catch(const char* error_message) {logger_ptr->error( [username, error_message]{return fmt::format("error on delete user from {} {}", username, error_message);});}
                
                logger_ptr->info( [username]{return fmt::format("user {} deleted", username);});
                return req->create_response().set_body("ok").done();
            } else {
                return req->create_response(restinio::status_unauthorized()).done();
            }
        });

    }

    void add_userrights(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_put(R"(/auth/add_userrights/:token([a-zA-Z0-9]+))", [pool_ptr, logger_ptr](auto req, auto) {
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

    void change_username(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_put(R"(/auth/change_username/:token([a-zA-Z0-9]+))", [pool_ptr, logger_ptr](auto req, auto) {
            std::string token = url::get_last_url_arg(req->header().path());

            if(token.empty()) {
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }

            if(!is_authed(token, pool_ptr)) {
                return req->create_response(restinio::status_unauthorized()).done();
            }

            std::string username = hashing::string_from_hash(token);

            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());

            if (new_body.HasMember("new_username")) {
                std::string new_username = new_body["new_username"].GetString();
                try {    
                    cp::query change_username("UPDATE \"user\" SET \"username\"=($1) WHERE \"username\"=($2);");
                    auto tx = cp::tx(*pool_ptr, change_username);
                    change_username(new_username, username);
                    tx.commit();
                    std::string new_token = hashing::hash_from_string(new_username);
                    logger_ptr->info( [username, new_username]{return fmt::format("user {} changed username to {}", username, new_username);});
                    return req->create_response().set_body("{\"token\": \"" + new_token + "\"}").done();
                } catch (const pqxx::unique_violation& e) {
                    logger_ptr->info( [username, new_username]{return fmt::format("username {} is already taken", new_username);});
                    return req->create_response(restinio::status_forbidden()).set_body("username is occupied").done();
                }
            }
            else {
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }
        });
    }
    void change_password(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_put(R"(/auth/change_password/:token([a-zA-Z0-9]+))", [pool_ptr, logger_ptr](auto req, auto) {
            std::string token = url::get_last_url_arg(req->header().path());

            if(token.empty()) {
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }

            std::string username = hashing::string_from_hash(token);

            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());

            bool unlogin = false;

            if(new_body.HasMember("unlogin")) {
                unlogin = new_body["unlogin"].GetBool();
            }

            if (new_body.HasMember("new_password")) {
                std::string new_password = new_body["new_password"].GetString();

                cp::query change_password("UPDATE \"user\" SET \"passwdhash\"=($1) WHERE \"username\"=($2);");
                auto tx = cp::tx(*pool_ptr, change_password);
                change_password(std::to_string(std::hash<std::string>{}(new_password)), username);
                tx.commit();
                
                if(unlogin) {
                    hashing::defele_from_hash(token);
                }

                logger_ptr->info( [username]{return fmt::format("user {} changed password", username);});
                return req->create_response().set_body("ok").done();
            }
            else {
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }
        });
    }

    void add_new_user(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_post("/auth/add_user", [pool_ptr, logger_ptr](auto req, auto) {
            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());

            if (!new_body.HasMember("token") || !auth::is_admin(new_body["token"].GetString(), pool_ptr)) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if (new_body.HasMember("input")) {
                std::string input = new_body["input"].GetString();
                hashing::add_new_user(input);                
                return req->create_response().set_body("ok").done();
            } else {
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }
        });
    }
}
