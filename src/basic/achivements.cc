#include "achivements.h"

namespace achivements {
    pqxx::result user_achivements(std::string username, std::shared_ptr<cp::connection_pool> pool_ptr) {
        cp::query get_user_achivements("select * from \"user_achivements\" right join \"achivements\" on \"user_achivements\".achivement_id = \"achivements\".achivement_id where \"user_achivements\".username = ($1) order by \"achivement_tree\" asc, \"level\" desc;");

        auto tx = cp::tx(*pool_ptr, get_user_achivements);
        
        pqxx::result result = get_user_achivements(username);

        if(result.empty()) {
            return {};
        }
        return result;
    }

    bool add_achivement(std::string username, int achivement_id, std::shared_ptr<cp::connection_pool> pool_ptr) {
        cp::query add_achivement("INSERT INTO \"user_achivements\" (username, achivement_id, stage) VALUES ($1, $2, 1) ON CONFLICT (username, achivement_id) DO UPDATE SET stage = user_achivements.stage + 1;");

        auto tx = cp::tx(*pool_ptr, add_achivement);
        
        try {
            add_achivement(username, achivement_id);
            tx.commit();
        } catch (...) {
            return -1;
        }

        return 0;
    }

    bool delete_achivement(std::string username, int achivement_id, std::shared_ptr<cp::connection_pool> pool_ptr) {
        cp::query delete_achivement("DELETE FROM \"user_achivements\" WHERE username=($1) AND achivement_id=($2);");

        auto tx = cp::tx(*pool_ptr, delete_achivement);
        
        try {
            delete_achivement(username, achivement_id);
            tx.commit();
        } catch (...) {
            return false;
        }
        return true;
    }

    pqxx::result achivements_tree(int tree_id, std::shared_ptr<cp::connection_pool> pool_ptr) {
        cp::query get_achivements_tree("SELECT * FROM \"achivements\" WHERE achivement_tree=($1) order by level DESC;");

        auto tx = cp::tx(*pool_ptr, get_achivements_tree);
        
        pqxx::result result = get_achivements_tree(tree_id);

        if(result.empty()) {
            return {};
        }
        return result;
    }

    pqxx::result achivement_info(int achivement_id, std::shared_ptr<cp::connection_pool> pool_ptr) {
        cp::query get_achivement_info("SELECT * FROM \"achivements\" WHERE achivement_id=($1);");

        auto tx = cp::tx(*pool_ptr, get_achivement_info);
        
        pqxx::result result = get_achivement_info(achivement_id);

        if(result.empty()) {
            return {};
        }
        return result;
    }

    int create_achivement(std::string name, int tree_id, int level, std::string pic, std::string description, int stages, std::shared_ptr<cp::connection_pool> pool_ptr) {
        cp::query create_achivement("INSERT INTO \"achivements\" (achivement_name, achivement_tree, level, achivement_pic, achivement_decsription, stages) VALUES ($1, $2, $3, $4, $5, $6) RETURNING achivement_id;");

        auto tx = cp::tx(*pool_ptr, create_achivement);
        pqxx::result id = {};
        try {
            id = create_achivement(name, tree_id, level, pic, description, stages);
            tx.commit();
        } catch (...) {
            return -1;
        }

        return id[0]["achivement_id"].as<int>();
    }
}

namespace achivements::server {
    void user_achivemets(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/achivements/user/:username([a-zA-Z0-9\-]+))", [pool_ptr, logger_ptr](auto req, auto) {
            auto qrl = req->header().path();

            std::string username = url::get_last_url_arg(qrl);

            if(username == "user" || username.empty()) {
                return req->create_response(restinio::status_bad_request()).done();
            }
            
            pqxx::result result = achivements::user_achivements(username, pool_ptr);
            
            if(result.empty()) {
                return req->create_response(restinio::status_bad_gateway()).done();
            }
            return req->create_response().set_body(cp::serialize(result)).done();
        });
    }

    void add_achivement(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_post("/achivements/add", [pool_ptr, logger_ptr](auto req, auto) {
            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());

            if (!new_body.HasMember("username") || !new_body.HasMember("achivement_id") || !new_body.HasMember("token")) {
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }

            if (!auth::is_admin(new_body["token"].GetString(), pool_ptr)) {
                return req->create_response(restinio::status_unauthorized()).done();
            }

            achivements::add_achivement(new_body["username"].GetString(), new_body["achivement_id"].GetInt(), pool_ptr);
            return req->create_response().set_body("ok").done();
        });
    }

    void delete_achivement(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_delete("/achivements/delete", [pool_ptr, logger_ptr](auto req, auto) {
            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());

            if (!new_body.HasMember("username") || !new_body.HasMember("achivement_id") || !new_body.HasMember("token")) {
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }

            if (!auth::is_admin(new_body["token"].GetString(), pool_ptr)) {
                return req->create_response(restinio::status_unauthorized()).done();
            }

            achivements::delete_achivement(new_body["username"].GetString(), new_body["achivement_id"].GetInt(), pool_ptr);
            return req->create_response().set_body("ok").done();
        });
    }

    void achivements_tree(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/achivements/tree/:tree_id([0-9]+))", [pool_ptr, logger_ptr](auto req, auto params) {
            auto qrl = req->header().path();

            std::string tree_id = url::get_last_url_arg(qrl);

            if(tree_id == "tree" || tree_id.empty()) {
                return req->create_response(restinio::status_bad_request()).done();
            }

            int id = std::stoi(tree_id);

            pqxx::result result = achivements::achivements_tree(id, pool_ptr);
            
            if(result.empty()) {
                return req->create_response(restinio::status_bad_gateway()).done();
            }
            return req->create_response().set_body(cp::serialize(result)).done();
        });
    }

    void achivement_info(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/achivements/info/:achivement_id([0-9]+))", [pool_ptr, logger_ptr](auto req, auto params) {
            auto qrl = req->header().path();

            std::string achivement_id = url::get_last_url_arg(qrl);

            if(achivement_id == "info" || achivement_id.empty()) {
                return req->create_response(restinio::status_bad_request()).done();
            }

            int id = std::stoi(achivement_id);

            pqxx::result result = achivements::achivement_info(id, pool_ptr);
            
            if(result.empty()) {
                return req->create_response(restinio::status_bad_gateway()).done();
            }
            return req->create_response().set_body(cp::serialize(result)).done();
        });
    }

    void create_achivement(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_post("/achivements/create", [pool_ptr, logger_ptr](auto req, auto) {
            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());

            if (!new_body.HasMember("token") || !auth::is_admin(new_body["token"].GetString(), pool_ptr)) {
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
                if(new_body.HasMember("stages")) {
                    int stages = new_body["stages"].GetInt();
                }
                if(!url::validate_pic_path(pic)) {
                    return req->create_response(restinio::status_bad_request()).done();
                }
                result = achivements::create_achivement(name, tree_id, level, pic, description, stages, pool_ptr);
                if (result == -1) {
                    return req->create_response(restinio::status_bad_request()).done();
                }
                std::string response = fmt::format("{{\"achivement_id\":{}}}", result);
                return req->create_response().set_body(response).done();
            } else {
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }
        });
    }
}