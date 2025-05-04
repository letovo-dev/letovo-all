#include "achivements.h"

namespace achivements {
    pqxx::result full_user_achivements(std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());

        std::vector<std::string> params = {username};

        pqxx::result result = con->execute_params("select * from \"user_achivements\" right join \"achivements\" on \"user_achivements\".achivement_id = \"achivements\".achivement_id where \"user_achivements\".username = ($1) or \"user_achivements\".username is null order by \"achivement_tree\" asc, \"level\" desc;", params);

        pool_ptr->returnConnection(std::move(con));

        if (result.empty()) {
            return {};
        }
        return result;
    }

    pqxx::result user_achivements(std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());

        std::vector<std::string> params = {username};

        pqxx::result result = con->execute_params("select * from \"user_achivements\" right join \"achivements\" on \"user_achivements\".achivement_id = \"achivements\".achivement_id where \"user_achivements\".username = ($1) order by \"achivement_tree\" asc, \"level\" desc;", params);

        pool_ptr->returnConnection(std::move(con));

        if (result.empty()) {
            return {};
        }
        return result;
    }


    bool add_achivement(std::string username, int achivement_id, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());

        std::vector<std::string> params = {username, std::to_string(achivement_id)};

        try {
            con->execute_params("INSERT INTO \"user_achivements\" (username, achivement_id, stage) VALUES ($1, $2, 1) ON CONFLICT (username, achivement_id) DO UPDATE SET stage = user_achivements.stage + 1;", params, true);
        } catch (...) {
            pool_ptr->returnConnection(std::move(con));
            return -1;
        }

        pool_ptr->returnConnection(std::move(con));
        return 0;
    }

    bool delete_achivement(std::string username, int achivement_id, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());

        std::vector<std::string> params = {username, std::to_string(achivement_id)};

        try {
            con->execute_params("DELETE FROM \"user_achivements\" WHERE username=($1) AND achivement_id=($2);", params, true);

        } catch (...) {
            pool_ptr->returnConnection(std::move(con));
            return false;
        }
        pool_ptr->returnConnection(std::move(con));
        return true;
    }

    pqxx::result achivements_tree(int tree_id, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());

        std::vector<int> params = {tree_id};

        pqxx::result result = con->execute_params("SELECT * FROM \"achivements\" WHERE achivement_tree=($1) order by level DESC;", params);

        pool_ptr->returnConnection(std::move(con));

        if (result.empty()) {
            return {};
        }
        return result;
    }

    pqxx::result achivement_info(int achivement_id, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());

        std::vector<int> params = {achivement_id};

        pqxx::result result = con->execute_params("SELECT * FROM \"achivements\" WHERE achivement_id=($1);", params);

        pool_ptr->returnConnection(std::move(con));

        if (result.empty()) {
            return {};
        }
        return result;
    }

    int create_achivement(std::string name, int tree_id, int level, std::string pic, std::string description, int stages, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        std::cout << "here2\n";
        
        auto con = std::move(pool_ptr->getConnection());

        std::vector<std::string> params = {name, std::to_string(tree_id), std::to_string(level), pic, description, std::to_string(stages)};

        std::cout << "here3\n";
        pqxx::result id = {};
        try {
            id = con->execute_params("INSERT INTO \"achivements\" (achivement_name, achivement_tree, level, achivement_pic, achivement_decsription, stages) VALUES ($1, $2, $3, $4, $5, $6) RETURNING achivement_id;", params, true);
            std::cout << "here3.5\n";
        } catch (...) {
            std::cout << "here4\n";
            pool_ptr->returnConnection(std::move(con));
            return -1;
        }
        
        pool_ptr->returnConnection(std::move(con));
        return id[0]["achivement_id"].as<int>();
    }

    std::vector<std::string> achivement_pictures() {
        std::vector<std::string> pictures;
        for (const auto& entry : std::filesystem::directory_iterator(Config::giveMe().pages_config.achivements_path.absolute)) {
            if (std::filesystem::is_regular_file(entry.status())) {
                pictures.push_back(Config::giveMe().pages_config.achivements_path.relative + entry.path().filename().string());
            }
        }
        return pictures;
    }
} // namespace achivements

namespace achivements::server {
    void user_achivemets(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/achivements/user/:username([a-zA-Z0-9\-]+))", [pool_ptr, logger_ptr](auto req, auto) {
            auto qrl = req->header().path();

            std::string username = url::get_last_url_arg(qrl);

            if (username == "user" || username.empty()) {
                return req->create_response(restinio::status_bad_request()).done();
            }
            
            pqxx::result result = achivements::user_achivements(username, pool_ptr);

            if (result.empty()) {
                return req->create_response(restinio::status_bad_gateway()).done();
            }
            return req->create_response().set_body(cp::serialize(result))
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
    }

    void full_user_achivemets(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/achivements/user/full/:username([a-zA-Z0-9\-]+))", [pool_ptr, logger_ptr](auto req, auto) {
            auto qrl = req->header().path();

            std::string username = url::get_last_url_arg(qrl);

            logger_ptr->info([username] { return fmt::format("username = {}, {}", username, username == "full" || username.empty()); });

            if (username == "full" || username.empty()) {
                return req->create_response(restinio::status_bad_request()).done();
            }
            pqxx::result result;
            try {
                result = achivements::full_user_achivements(username, pool_ptr);
            } catch (...) {
                return req->create_response(restinio::status_internal_server_error()).done();
            }

            if (result.empty()) {
                return req->create_response(restinio::status_ok())
                    .set_body("{}")
                    .done();
            }
            return req->create_response().set_body(cp::serialize(result))
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
    }

    void add_achivement(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_post("/achivements/add", [pool_ptr, logger_ptr](auto req, auto) {
            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());

            if (!new_body.HasMember("username") || !new_body.HasMember("achivement_id")) {
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }

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

            achivements::add_achivement(new_body["username"].GetString(), new_body["achivement_id"].GetInt(), pool_ptr);
            return req->create_response().set_body("ok")
                .append_header("Content-Type", "text/plain; charset=utf-8")
                .done();
        });
    }

    void delete_achivement(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_delete("/achivements/delete", [pool_ptr, logger_ptr](auto req, auto) {
            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());

            if (!new_body.HasMember("username") || !new_body.HasMember("achivement_id")) {
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }

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

            achivements::delete_achivement(new_body["username"].GetString(), new_body["achivement_id"].GetInt(), pool_ptr);
            return req->create_response().set_body("ok")
                .append_header("Content-Type", "text/plain; charset=utf-8")
                .done();
        });
    }

    void achivements_tree(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/achivements/tree/:tree_id([0-9]+))", [pool_ptr, logger_ptr](auto req, auto params) {
            auto qrl = req->header().path();

            std::string tree_id = url::get_last_url_arg(qrl);

            if (tree_id == "tree" || tree_id.empty()) {
                return req->create_response(restinio::status_bad_request()).done();
            }

            int id = std::stoi(tree_id);

            pqxx::result result = achivements::achivements_tree(id, pool_ptr);

            if (result.empty()) {
                return req->create_response(restinio::status_bad_gateway()).done();
            }
            return req->create_response().set_body(cp::serialize(result))
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
    }

    void achivement_info(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/achivements/info/:achivement_id([0-9]+))", [pool_ptr, logger_ptr](auto req, auto params) {
            auto qrl = req->header().path();

            std::string achivement_id = url::get_last_url_arg(qrl);

            if (achivement_id == "info" || achivement_id.empty()) {
                return req->create_response(restinio::status_bad_request()).done();
            }

            int id = std::stoi(achivement_id);

            pqxx::result result = achivements::achivement_info(id, pool_ptr);

            if (result.empty()) {
                return req->create_response(restinio::status_bad_gateway()).done();
            }
            return req->create_response().set_body(cp::serialize(result))
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
    }

    void create_achivement(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_post("/achivements/create", [pool_ptr, logger_ptr](auto req, auto) {
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

            int result;
            if (new_body.HasMember("name") && new_body.HasMember("tree_id") && new_body.HasMember("level") && new_body.HasMember("pic") && new_body.HasMember("description")) {
                std::string name = new_body["name"].GetString();
                int tree_id = new_body["tree_id"].GetInt();
                int level = new_body["level"].GetInt();
                std::string pic = new_body["pic"].GetString();
                std::string description = new_body["description"].GetString();
                int stages = 1;
                if (new_body.HasMember("stages")) {
                    int stages = new_body["stages"].GetInt();
                }
                if (!url::validate_pic_path(pic)) {
                    logger_ptr->info([]{return "wrong pic path";});
                    return req->create_response(restinio::status_bad_request()).done();
                }
                std::cout << "here\n";
                result = achivements::create_achivement(name, tree_id, level, pic, description, stages, pool_ptr);
                if (result == -1) {
                    return req->create_response(restinio::status_bad_request()).done();
                }
                std::cout << result << std::endl;
                std::string response = fmt::format("{{\"achivement_id\":{}}}", result);
                return req->create_response().set_body(response)
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
            } else {
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }
        });
    }

    void achivement_pictures(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get("/achivements/pictures", [logger_ptr](auto req, auto) {
            return req->create_response().set_body(cp::serialize(achivements::achivement_pictures()))
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
    }

} // namespace achivements::server
