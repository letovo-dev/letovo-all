#include "./auth.h"

namespace auth {
    std::string get_username(std::string token, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto decoded = hashing::string_from_hash(token);

        auto con = std::move(pool_ptr->getConnection());

        std::vector<std::string> params = {decoded};

        pqxx::result result = con->execute_params("SELECT * FROM \"user\" WHERE \"username\"=($1);", params);

        pool_ptr->returnConnection(std::move(con));

        if (result.empty()) {
            return "";
        }
        return decoded;
    }

    bool is_authed(std::string token, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        return get_username(token, pool_ptr) != "";
    }

    bool is_authed_by_body(std::string req_body, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        rapidjson::Document new_body;
        new_body.Parse(req_body.c_str());

        if (new_body.HasMember("token")) {
            std::string token = new_body["token"].GetString();
            return is_authed(token, pool_ptr);
        }
        return false;
    }

    bool is_admin(std::string token, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto decoded = hashing::string_from_hash(token);

        return is_rights_by_username(decoded, pool_ptr);
    }

    bool is_user(std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        std::vector<std::string> params = {username};

        auto con = std::move(pool_ptr->getConnection());

        pqxx::result result = con->execute_params("SELECT * FROM \"user\" WHERE \"username\"=($1);", params);

        pool_ptr->returnConnection(std::move(con));

        if (result.empty()) {
            return false;
        }
        return true;
    }

    bool is_rights_by_username(std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::string rights) {
        std::vector<std::string> params = {username, rights};

        auto con = std::move(pool_ptr->getConnection());

        pqxx::result result = con->execute_params("SELECT * FROM \"user\" WHERE \"username\"=($1) AND \"userrights\"=($2);", params);

        pool_ptr->returnConnection(std::move(con));

        if (result.empty()) {
            return false;
        }
        return true;
    }

    bool is_active(std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        std::vector<std::string> params = {username};

        auto con = std::move(pool_ptr->getConnection());

        pqxx::result result = con->execute_params("SELECT active FROM \"user\" WHERE \"username\"=($1) and \"active\"=true;", params);

        pool_ptr->returnConnection(std::move(con));

        if (result.empty()) {
            return false;
        }
        return true;
    }

    bool auth(std::string username, std::string password, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        std::string passwordHash = std::to_string(std::hash<std::string>{}(password));
        std::vector<std::string> params = {username, passwordHash};

        auto con = std::move(pool_ptr->getConnection());

        pqxx::result result = con->execute_params("SELECT * FROM \"user\" WHERE \"username\"=($1) AND \"passwdhash\"=($2);", params);

        pool_ptr->returnConnection(std::move(con));

        if (result.empty()) {
            return false;
        }
        return true;
    }

    bool reg(std::string username, std::string password_hash, std::string userid, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        std::vector<std::string> params = {userid, username, password_hash};

        auto con = std::move(pool_ptr->getConnection());
        try {
            con->execute_params("INSERT INTO \"user\" (userid, username, passwdhash, userrights, jointime) VALUES($1, $2, $3, '', now());", params, true);
        } catch (const pqxx::unique_violation& e) {
            return false;
        }
        return true;
    }

    bool delete_user(std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        std::vector<std::string> params = {username};

        auto con = std::move(pool_ptr->getConnection());

        con->execute_params("DELETE FROM \"user\" WHERE \"username\"=($1);", params, true);

        pool_ptr->returnConnection(std::move(con));

        return true;
    }

    bool add_userrights(std::string username, std::string rights, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        std::vector<std::string> params = {rights, username};

        auto con = std::move(pool_ptr->getConnection());

        con->execute_params("UPDATE \"user\" SET \"userrights\"=($1) WHERE \"username\"=($2);", params, true);

        pool_ptr->returnConnection(std::move(con));

        return true;
    }

    bool change_username(std::string username, std::string new_username, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        std::vector<std::string> params = {new_username, username};

        auto con = std::move(pool_ptr->getConnection());

        con->execute_params("UPDATE \"user\" SET \"username\"=($1) WHERE \"username\"=($2);", params, true);

        pool_ptr->returnConnection(std::move(con));

        return true;
    }

    bool change_password(std::string username, std::string new_password, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        std::string new_password_hash = std::to_string(std::hash<std::string>{}(new_password));
        std::vector<std::string> params = {new_password_hash, username};

        auto con = std::move(pool_ptr->getConnection());

        con->execute_params("UPDATE \"user\" SET \"passwdhash\"=($1) WHERE \"username\"=($2);", params, true);

        pool_ptr->returnConnection(std::move(con));

        return true;
    }

    bool register_true(std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        std::vector<std::string> params = {username};

        auto con = std::move(pool_ptr->getConnection());

        con->execute_params("UPDATE \"user\" SET \"registered\"=true WHERE \"username\"=($1);", params, true);

        pool_ptr->returnConnection(std::move(con));

        return true;
    }
} // namespace auth

namespace auth::server {
    void enable_auth(std::unique_ptr<restinio::router::express_router_t<>>& router,
                     std::shared_ptr<cp::ConnectionsManager> pool_ptr,
                     std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_post("/auth/login", [pool_ptr, logger_ptr](auto req, auto) {
            std::string endpoint = req->remote_endpoint().address().to_string();
            logger_ptr->info([endpoint] { return fmt::format("auth request from {}", endpoint); });

            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());

            if (new_body.HasMember("login") && new_body.HasMember("password")) {
                std::string loginHeader = new_body["login"].GetString();
                std::string passwordHeader = new_body["password"].GetString();

                auto user = user::user_info(loginHeader, pool_ptr);

                if (!auth::auth(loginHeader, passwordHeader, pool_ptr)) {
                    return req->create_response(restinio::status_unauthorized()).append_header_date_field().connection_close().done();
                } else {
                    auto token = hashing::hash_from_string(loginHeader);

                    return req->create_response()
                        .set_body(cp::serialize(user))
                        .append_header("Authorization", token)
                        .append_header("Content-Type", "application/json; charset=utf-8")
                        .done();
                }
            }

            else {
                return req->create_response(restinio::status_non_authoritative_information()).append_header_date_field().connection_close().done();
            }
        });
    }

    void enable_reg(std::unique_ptr<restinio::router::express_router_t<>>& router,
                    std::shared_ptr<cp::ConnectionsManager> pool_ptr,
                    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_post("/auth/reg", [pool_ptr, logger_ptr](auto req, auto) {
            std::string endpoint = req->remote_endpoint().address().to_string();

            logger_ptr->info([endpoint] { return fmt::format("reg request from {}", endpoint); });

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

                    if (!auth::reg(userid, loginHeader, passwordHash, pool_ptr)) {
                        logger_ptr->warn([endpoint, loginHeader] { return fmt::format("user {} already exists", loginHeader); });
                        return req->create_response(restinio::status_forbidden())
                            .append_header("Content-Type", "application/json; charset=utf-8")
                            .set_body("{status: \"username is ocupied\"}")
                            .done();
                    }

                    auto token = hashing::hash_from_string(loginHeader);
                    logger_ptr->info([endpoint, loginHeader] { return fmt::format("new user with ip {} username {}", endpoint, loginHeader); });

                    return req->create_response()
                        .set_body(cp::serialize(user::user_info(loginHeader, pool_ptr)))
                        .append_header("Authorization", token)
                        .done();
                } catch (const char* error_message) {
                    logger_ptr->error([endpoint, error_message] { return fmt::format("error from {} {}", endpoint, error_message); });
                }
            }
            return req->create_response(restinio::status_non_authoritative_information()).done();
        });
    }

    void am_i_authed(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/auth/amiauthed)", [pool_ptr, logger_ptr](auto req, auto) {
            std::string token;
            try {
                token = req -> header().get_field("Bearer");
            } catch (const std::exception& e) {
                logger_ptr->info([]{return "can't get token";});
                return req->create_response(restinio::status_unauthorized()).done();
            }

            if (token.empty()) {
                logger_ptr->info([]{return "token is empty";});
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if (auth::is_authed(token, pool_ptr)) {
                return req->create_response(restinio::status_ok()).set_body("{\"status\": \"t\"}")
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
            } else {
                return req->create_response(restinio::status_ok()).set_body("{\"status\": \"f\"}")
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
            }
        });
    }

    void am_i_admin(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/auth/amiadmin)", [pool_ptr, logger_ptr](auto req, auto) {
            std::string token;
            try {
                token = req -> header().get_field("Bearer");
            } catch (const std::exception& e) {
                logger_ptr->info([]{return "can't get token";});
                return req->create_response(restinio::status_unauthorized()).done();
            }

            if (token.empty()) {
                logger_ptr->info([]{return "token is empty";});
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if (auth::is_admin(token, pool_ptr)) {
                return req->create_response(restinio::status_ok()).set_body("{\"status\": \"t\"}")
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
            } else {
                return req->create_response(restinio::status_ok()).set_body("{\"status\": \"f\"}")
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
            }
        });
    }

    void is_user_active(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/auth/isactive/:username(.*))", [pool_ptr](auto req, auto) {
            std::string username = url::get_last_url_arg(req->header().path());

            if (auth::is_active(username, pool_ptr)) {
                return req->create_response(restinio::status_ok()).set_body("{\"status\": \"t\"}")
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
            } else {
                return req->create_response(restinio::status_ok()).set_body("{\"status\": \"f\"}")
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
            }
        });
    }

    void is_user(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/auth/isuser/:username(.*))", [pool_ptr, logger_ptr](auto req, auto) {
            std::string username = url::get_last_url_arg(req->header().path());

            logger_ptr->info([username] { return fmt::format("is user request for {}", username); });

            if (auth::is_user(username, pool_ptr)) {
                return req->create_response(restinio::status_ok()).set_body("{\"status\": \"t\"}")
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
            } else {
                return req->create_response(restinio::status_ok()).set_body("{\"status\": \"f\"}")
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
            }
        });
    }

    void enable_delete(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_delete(R"(/auth/delete)", [pool_ptr, logger_ptr](auto req, auto) {
            std::string token;
            try {
                token = req -> header().get_field("Bearer");
            } catch (const std::exception& e) {
                logger_ptr->info([]{return "can't get token";});
                return req->create_response(restinio::status_unauthorized()).done();
            }

            if (token.empty()) {
                logger_ptr->info([]{return "token is empty";});
                return req->create_response(restinio::status_unauthorized()).done();
            }

            std::string username = hashing::string_from_hash(token);

            if (auth::is_authed(token, pool_ptr)) {
                if (auth::delete_user(username, pool_ptr)) {
                    logger_ptr->info([username] { return fmt::format("user {} deleted", username); });
                    return req->create_response().set_body("ok")
                    .append_header("Content-Type", "text/plain; charset=utf-8")
                    .done();
                } else {
                    return req->create_response(restinio::status_internal_server_error()).done();
                }
            } else {
                return req->create_response(restinio::status_unauthorized()).done();
            }
        });
    }

    void add_userrights(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_put(R"(/auth/add_userrights)", [pool_ptr, logger_ptr](auto req, auto) {
            std::string token;
            try {
                token = req -> header().get_field("Bearer");
            } catch (const std::exception& e) {
                logger_ptr->info([]{return "can't get token";});
                return req->create_response(restinio::status_unauthorized()).done();
            }

            if (token.empty()) {
                logger_ptr->info([]{return "token is empty";});
                return req->create_response(restinio::status_unauthorized()).done();
            }

            std::string username = hashing::string_from_hash(token);

            if (!auth::is_admin(token, pool_ptr)) {
                return req->create_response(restinio::status_unauthorized())
                .append_header("Content-Type", "application/json; charset=utf-8")
                .set_body("{status: \"ты даже не гражданин!\"}")
                .done();
            }
            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());

            if (new_body.HasMember("username") && new_body.HasMember("rights")) {
                std::string user = new_body["username"].GetString();
                std::string rights = new_body["rights"].GetString();

                if (auth::add_userrights(user, rights, pool_ptr)) {
                    logger_ptr->info([username, user, rights] { return fmt::format("user {} added rights {} to {}", username, rights, user); });
                    return req->create_response().set_body("ok")
                    .append_header("Content-Type", "application/json; charset=utf-8")
                    .done();
                } else {
                    return req->create_response(restinio::status_internal_server_error()).done();
                }
            } else {
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }
        });
    }

    void change_username(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_put(R"(/auth/change_username)", [pool_ptr, logger_ptr](auto req, auto) {
            std::string token;
            try {
                token = req -> header().get_field("Bearer");
            } catch (const std::exception& e) {
                logger_ptr->error([e] { return fmt::format("{}", e.what()); });
                return req->create_response(restinio::status_unauthorized()).done();
            }

            if (token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }

            if (!is_authed(token, pool_ptr)) {
                return req->create_response(restinio::status_unauthorized()).done();
            }

            std::string username = hashing::string_from_hash(token);

            logger_ptr->info([username] { return fmt::format("user {} change username request", username); });

            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());

            if (new_body.HasMember("new_username")) {
                std::string new_username = new_body["new_username"].GetString();
                if (!auth::change_username(username, new_username, pool_ptr)) {
                    return req->create_response(restinio::status_internal_server_error()).done();
                }
                std::string new_token = hashing::hash_from_string(new_username);
                logger_ptr->info([username, new_username] { return fmt::format("user {} changed username to {}", username, new_username); });
                return req->create_response().set_body("{\"token\": \"" + new_token + "\"}")
                    .append_header("Content-Type", "application/json; charset=utf-8")
                    .done();
            } else {
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }
        });
    }
    void change_password(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_put("/auth/change_password", [pool_ptr, logger_ptr](auto req, auto) {
            std::string token;
            try {
                token = req -> header().get_field("Bearer");
            } catch (const std::exception& e) {
                logger_ptr->info([]{return "can't get token";});
                return req->create_response(restinio::status_unauthorized()).done();
            }

            if (token.empty()) {
                logger_ptr->info([]{return "token is empty";});
                return req->create_response(restinio::status_unauthorized()).done();
            }

            std::string username = hashing::string_from_hash(token);

            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());

            bool unlogin = false;

            if (new_body.HasMember("unlogin")) {
                unlogin = new_body["unlogin"].GetBool();
            }

            if (new_body.HasMember("new_password")) {
                std::string new_password = new_body["new_password"].GetString();

                if (!auth::change_password(username, new_password, pool_ptr)) {
                    return req->create_response(restinio::status_internal_server_error()).done();
                }
                if (unlogin) {
                    hashing::defele_from_hash(token);
                }

                logger_ptr->info([username] { return fmt::format("user {} changed password", username); });
                return req->create_response().set_body("ok")
                    .append_header("Content-Type", "text/plain; charset=utf-8")
                    .done();
            } else {
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }
        });
    }

    void add_new_user(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_post("/auth/add_user", [pool_ptr, logger_ptr](auto req, auto) {
            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());

            std::string token;
            try {
                token = req -> header().get_field("Bearer");
            } catch (const std::exception& e) {
                logger_ptr->info([]{return "can't get token";});
                return req->create_response(restinio::status_unauthorized()).done();
            }

            if (token.empty()) {
                logger_ptr->info([]{return "token is empty";});
                return req->create_response(restinio::status_unauthorized()).done();
            }

            if (!auth::is_admin(token, pool_ptr)) {
                logger_ptr->info([]{return "not admin";});
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if (new_body.HasMember("input")) {
                std::string input = new_body["input"].GetString();
                hashing::add_new_user(input);
                return req->create_response().set_body("ok")
                    .append_header("Content-Type", "text/plain; charset=utf-8")
                    .done();
            } else {
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }
        });
    }

    void register_true(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_put("/auth/register_true", [pool_ptr, logger_ptr](auto req, auto) {
            std::string token;
            try {
                token = req -> header().get_field("Bearer");
            } catch (const std::exception& e) {
                logger_ptr->info([]{return "can't get token";});
                return req->create_response(restinio::status_unauthorized()).done();
            }

            if (token.empty()) {
                logger_ptr->info([]{return "token is empty";});
                return req->create_response(restinio::status_unauthorized()).done();
            }

            std::string username = hashing::string_from_hash(token);

            if (auth::register_true(username, pool_ptr)) {
                return req->create_response().set_body("ok")
                    .append_header("Content-Type", "text/plain; charset=utf-8")
                    .done();
            } else {
                return req->create_response(restinio::status_internal_server_error()).done();
            }
            
        });
    }
} // namespace auth::server
