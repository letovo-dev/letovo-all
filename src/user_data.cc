#include "user_data.h"

namespace user {
    pqxx::row role(int role_id, std::shared_ptr<cp::connection_pool> pool_ptr) {
        cp::query get_role("select * from \"roles\" where roleid=($1);");

        auto tx = cp::tx(*pool_ptr, get_role);
        
        pqxx::result result = get_role(role_id);

        if(result.empty()) {
            return {};
        }
        return result[0];
    }

    int role_id(std::string role, std::string department, std::shared_ptr<cp::connection_pool> pool_ptr) {
        cp::query get_role_id("select roleid from \"roles\" where rolename=($1) and department=($2);");

        auto tx = cp::tx(*pool_ptr, get_role_id);
        
        pqxx::result result = get_role_id(role, department);

        if(result.empty()) {
            return -1;
        }
        return result[0]["roleid"].as<int>();
    }

    pqxx::row user_role(std::string username, std::shared_ptr<cp::connection_pool> pool_ptr) {
        cp::query get_user_role("select \"roles\".department, \"roles\".rolename from \"user\" left join \"roles\" on \"user\".role = \"roles\".roleid where \"user\".username=($1);");

        auto tx = cp::tx(*pool_ptr, get_user_role);
        
        pqxx::result result = get_user_role(username);

        if(result.empty()) {
            return {};
        }
        return result[0];
    }

    pqxx::row user_info(std::string username, std::shared_ptr<cp::connection_pool> pool_ptr) {
        cp::query get_user_info("select \"user\".userid, \"user\".username, \"user\".userrights, \"user\".jointime, \"user\".avatar_pic, \"user\".active, \"roles\".department, \"roles\".rolename from \"user\" left join \"roles\" on \"user\".role = \"roles\".roleid where \"user\".username=($1);");

        auto tx = cp::tx(*pool_ptr, get_user_info);
        
        pqxx::result result = get_user_info(username);

        if(result.empty()) {
            return {};
        }
        
        return result[0];
    }

    pqxx::result user_roles(std::string username, std::shared_ptr<cp::connection_pool> pool_ptr) {
        cp::query get_user_roles("select * from \"useroles\" where username=($1);");

        auto tx = cp::tx(*pool_ptr, get_user_roles);
        
        pqxx::result result = get_user_roles(username);

        if(result.empty()) {
            return {};
        }
        return result;
    }

    pqxx::result best_user_roles(std::string username, std::shared_ptr<cp::connection_pool> pool_ptr) {
        cp::query get_user_roles("SELECT u.username , r.rolename , r.department, r.rang\
FROM roles r left join \"useroles\" u on r.roleid = u.roleid \
WHERE r.rang = ( \
    SELECT MAX(rang) \
    FROM roles \
    WHERE department = r.department \
) and u.username = 'scv-7';");

        auto tx = cp::tx(*pool_ptr, get_user_roles);
        
        pqxx::result result = get_user_roles(username);

        if(result.empty()) {
            return {};
        }
        return result;
    }


    bool add_user_role(std::string username, std::string role, std::string department, std::shared_ptr<cp::connection_pool> pool_ptr) {
        int role_id = user::role_id(role, department, pool_ptr);
        cp::query add_user_role("INSERT INTO \"useroles\" (username, roleid) VALUES($1, $2);");
        cp::query change_user_role("UPDATE \"user\" SET role=($1) WHERE username=($2);");
        auto tx = cp::tx(*pool_ptr, add_user_role, change_user_role);
        try {        
            add_user_role(username, role_id);
            change_user_role(role_id, username);
        } catch (const pqxx::unique_violation& e) {
            return false;
        }
        tx.commit();
        return true;
    }

    bool add_user_role(std::string username, int role_id, std::shared_ptr<cp::connection_pool> pool_ptr) {
        cp::query add_user_role("INSERT INTO \"useroles\" (username, roleid) VALUES($1, $2);");
        cp::query change_user_role("UPDATE \"user\" SET role=($1) WHERE username=($2);");
        auto tx = cp::tx(*pool_ptr, add_user_role, change_user_role);
        try {        
            add_user_role(username, role_id);
            change_user_role(role_id, username);
        } catch (const pqxx::unique_violation& e) {
            return false;
        }
        tx.commit();
        return true;
    }
}

namespace user::server {
    void user_info(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/user/:username([a-zA-Z0-9\-]+))", [pool_ptr, logger_ptr](auto req, auto params) {
            auto qrl = req->header().path();

            std::string username = url::get_last_url_arg(qrl);

            if(username == "user" || username.empty()) {
                return req->create_response(restinio::status_bad_request()).done();
            }

            pqxx::row result = user::user_info(username, pool_ptr);
            
            if(result.empty()) {
                return req->create_response(restinio::status_bad_gateway()).done();
            }
            return req->create_response().set_body(cp::serialize(result)).done();
        });
    }

    void user_roles(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/user/:username([a-zA-Z0-9\-]+))", [pool_ptr, logger_ptr](auto req, auto params) {
            auto qrl = req->header().path();

            std::string username = url::get_last_url_arg(qrl);

            if(username == "user" || username.empty()) {
                return req->create_response(restinio::status_bad_request()).done();
            }

            pqxx::result result = user::best_user_roles(username, pool_ptr);
            
            if(result.empty()) {
                return req->create_response(restinio::status_bad_gateway()).done();
            }
            return req->create_response().set_body(cp::serialize(result)).done();
        });
    }

    void add_user_role(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_post("/user/add_role", [pool_ptr, logger_ptr](auto req, auto) {
            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());

            if (!new_body.HasMember("username") || !new_body.HasMember("token")) {
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }

            if (new_body.HasMember("token") && !auth::is_admin(new_body["token"].GetString(), pool_ptr)) {
                return req->create_response(restinio::status_unauthorized()).done();
            }

            if (new_body.HasMember("role_id")) {
                user::add_user_role(new_body["username"].GetString(), new_body["role_id"].GetInt(), pool_ptr);
            } else if(new_body.HasMember("role") && new_body.HasMember("department")) {
                user::add_user_role(new_body["username"].GetString(), new_body["role"].GetString(), new_body["department"].GetString(), pool_ptr);
            } else {
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }
            return req->create_response().set_body("ok").done();
        });
    }

    void delete_user_role(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr){
        router.get()->http_post("/user/delete_role", [pool_ptr, logger_ptr](auto req, auto) {
            return req->create_response(restinio::status_not_implemented()).done();
        });
    }
}