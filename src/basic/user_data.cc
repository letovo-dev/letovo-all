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

    int role_id(std::string role, int department, std::shared_ptr<cp::connection_pool> pool_ptr) {
        cp::query get_role_id("select roleid from \"roles\" where rolename=($1) and department=($2);");

        auto tx = cp::tx(*pool_ptr, get_role_id);
        
        pqxx::result result = get_role_id(role, department);

        if(result.empty()) {
            return -1;
        }
        return result[0]["roleid"].as<int>();
    }

    pqxx::row user_role(std::string username, std::shared_ptr<cp::connection_pool> pool_ptr) {
        cp::query get_user_role("select \"roles\".departmentid, \"roles\".rolename from \"user\" left join \"roles\" on \"user\".role = \"roles\".roleid where \"user\".username=($1);");

        auto tx = cp::tx(*pool_ptr, get_user_role);
        
        pqxx::result result = get_user_role(username);

        if(result.empty()) {
            return {};
        }
        return result[0];
    }

    pqxx::row user_info(std::string username, std::shared_ptr<cp::connection_pool> pool_ptr) {
        cp::query get_user_info("select \"user\".userid, \"user\".username, \"user\".userrights, \"user\".jointime, \"user\".avatar_pic, \"user\".active, \"roles\".departmentid, \"roles\".rolename from \"user\" left join \"roles\" on \"user\".role = \"roles\".roleid where \"user\".username=($1);");

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
        // TODO: not working
        cp::query get_user_roles("SELECT distinct \"useroles\".username , \"roles\".rolename , \"roles\".departmentid, \"roles\".rang FROM \"roles\" left join \"useroles\" on \"roles\".roleid = \"useroles\".roleid WHERE \"roles\".rang = (SELECT MAX(rang) FROM roles WHERE departmentid = \"roles\".departmentid) and \"useroles\".username = ($1);");

        auto tx = cp::tx(*pool_ptr, get_user_roles);
        
        pqxx::result result = get_user_roles(username);

        if(result.empty()) {
            return {};
        }
        return result;
    }


    bool add_user_role(std::string username, std::string role, int department, std::shared_ptr<cp::connection_pool> pool_ptr) {
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

    int create_role(std::string role, int department, int rang, int payment, std::shared_ptr<cp::connection_pool> pool_ptr) {
        cp::query create_role("INSERT INTO \"roles\" (rolename, department, rang, payment) VALUES($1, $2, $3, $4) returning \"roleid\";");
        auto tx = cp::tx(*pool_ptr, create_role);
        pqxx::result result;
        try {        
            result = create_role(role, department, rang, payment);
        } catch (const pqxx::unique_violation& e) {
            return -1;
        }
        tx.commit();
        return result[0]["roleid"].as<int>();
    }

    pqxx::result department_roles(int department, std::shared_ptr<cp::connection_pool> pool_ptr) {
        cp::query get_department_roles("SELECT * FROM \"roles\" WHERE departmentid=($1) order by rang;");

        auto tx = cp::tx(*pool_ptr, get_department_roles);
        
        pqxx::result result = get_department_roles(department);

        if(result.empty()) {
            return {};
        }
        return result;
    }

    std::string department_name(int department, std::shared_ptr<cp::connection_pool> pool_ptr) {
        cp::query get_department_name("SELECT departmentname FROM \"department\" WHERE departmentid=($1);");

        auto tx = cp::tx(*pool_ptr, get_department_name);
        
        pqxx::result result = get_department_name(department);

        if(result.empty()) {
            return "";
        }
        return result[0]["departmentname"].as<std::string>();
    }
    
    int department_id(std::string department, std::shared_ptr<cp::connection_pool> pool_ptr) {
        cp::query get_department_id("SELECT departmentid FROM \"department\" WHERE departmentname=($1);");

        auto tx = cp::tx(*pool_ptr, get_department_id);
        
        pqxx::result result = get_department_id(department);

        if(result.empty()) {
            return -1;
        }
        return result[0]["departmentid"].as<int>();
    }

    int best_users_role_by_department(std::string username, int department, std::shared_ptr<cp::connection_pool> pool_ptr) {
        if(department_name(department, pool_ptr) == "") {
            return -1;
        }        
        cp::query best_role = cp::query("select \"roles\".roleid from \"useroles\" left join \"roles\" on \"useroles\".roleid = \"roles\".roleid where \"useroles\".username = ($1) and \"roles\".departmentid=($2) and \"roles\".rang = (SELECT MAX(\"roles\".rang) FROM \"roles\" left join \"useroles\" on \"roles\".roleid = \"useroles\".roleid WHERE \"roles\".departmentid = ($2) and \"useroles\".username = ($1));");

        pqxx::result result; {
            auto tx = cp::tx(*pool_ptr, best_role);
            
            result = best_role(username, department);
        }

        if(result.empty()) {
            return user::starter_role(department, pool_ptr);
        }
        return result[0]["roleid"].as<int>();
    }

    int set_users_department(std::string username, int department, std::shared_ptr<cp::connection_pool> pool_ptr) {
        int best_users_role = best_users_role_by_department(username, department, pool_ptr);
        if (best_users_role == -1) {
            return -1;
        }
        cp::query change_user_role("UPDATE \"user\" SET role=($1) WHERE username=($2);");
        
        auto tx = cp::tx(*pool_ptr, change_user_role);

        change_user_role(best_users_role, username);

        tx.commit();

        return best_users_role;
    }

    int set_users_department(std::string username, std::string department, std::shared_ptr<cp::connection_pool> pool_ptr) {
        int department_id = user::department_id(department, pool_ptr);
        if(department_id == -1) {
            return -1;
        }
        return set_users_department(username, department_id, pool_ptr);
    }

    pqxx::result all_departments(std::shared_ptr<cp::connection_pool> pool_ptr) {
        cp::query all_departments = cp::query("SELECT * FROM \"department\";");

        auto tx = cp::tx(*pool_ptr, all_departments);
        
        pqxx::result result = all_departments();

        if(result.empty()) {
            return {};
        }
        return result;
    }


    int starter_role(int department, std::shared_ptr<cp::connection_pool> pool_ptr) {        
        cp::query starter_role = cp::query("select roleid from \"roles\" where departmentid=($1) and rang=0;");

        auto tx = cp::tx(*pool_ptr, starter_role);
        
        pqxx::result result = starter_role(department);

        if(result.empty()) {
            return -1;
        }
        return result[0]["roleid"].as<int>();
    }
    
    int starter_role(std::string department, std::shared_ptr<cp::connection_pool> pool_ptr) {
        int department_id = user::department_id(department, pool_ptr);
        if(department_id == -1) {
            return -1;
        }
        return user::starter_role(department_id, pool_ptr);
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
        // TODO: not working as it should: shows only the last role, not all of them idk why
        router.get()->http_get(R"(/user/roles/:username([a-zA-Z0-9\-]+))", [pool_ptr, logger_ptr](auto req, auto params) {
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

            std::string token = new_body["token"].GetString();

            logger_ptr->info( [token, pool_ptr]{return fmt::format("token = {}, admin = {}", token, auth::is_admin(token, pool_ptr));});

            if (!new_body.HasMember("username") || !new_body.HasMember("token")) {
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }

            if (new_body.HasMember("token") && !auth::is_admin(new_body["token"].GetString(), pool_ptr)) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if (new_body.HasMember("role_id")) {
                user::add_user_role(new_body["username"].GetString(), new_body["role_id"].GetInt(), pool_ptr);
                return req->create_response().set_body("ok").done();
            } else if(new_body.HasMember("role") && new_body.HasMember("department")) {
                user::add_user_role(new_body["username"].GetString(), new_body["role"].GetString(), new_body["department"].GetInt(), pool_ptr);
                return req->create_response().set_body("ok").done();
            } else {
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }
            return req->create_response(restinio::status_internal_server_error()).done();
        });
    }

    void delete_user_role(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr){
        router.get()->http_delete("/user/delete_role", [pool_ptr, logger_ptr](auto req, auto) {
            return req->create_response(restinio::status_not_implemented()).done();
        });
    }

    void create_role(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr){
        router.get()->http_post("/user/create_role", [pool_ptr, logger_ptr](auto req, auto) {
            
            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());

            if (!new_body.HasMember("token") || !auth::is_admin(new_body["token"].GetString(), pool_ptr)) {
                return req->create_response(restinio::status_unauthorized()).done();
            }

            int payment = new_body.HasMember("payment") ? new_body["payment"].GetInt() : 0;

            if (new_body.HasMember("role") && new_body.HasMember("department") && new_body.HasMember("rang")) {
                int roleid = user::create_role(new_body["role"].GetString(), new_body["department"].GetInt(), new_body["rang"].GetInt(), payment, pool_ptr);
                logger_ptr->info( [roleid]{return fmt::format("created role with roleid = {}", roleid);});
                return req->create_response().set_body("ok").done();
            } else {
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }
            return req->create_response(restinio::status_internal_server_error()).done();
        });
    }

    void department_roles(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr){
        router.get()->http_get(R"(/user/department/roles/:department([0-9]+))", [pool_ptr, logger_ptr](auto req, auto params) {
            auto qrl = req->header().path();

            std::string department = url::get_last_url_arg(qrl);
            logger_ptr->info( [department]{return fmt::format("department = {}", department);});

            if(department == "roles" || department.empty()) {
                return req->create_response(restinio::status_bad_request()).done();
            }

            int department_id = std::stoi(department);

            pqxx::result result = user::department_roles(department_id, pool_ptr);
            
            if(result.empty()) {
                return req->create_response(restinio::status_bad_gateway()).done();
            }
            return req->create_response().set_body(cp::serialize(result)).done();
        });
    }

    void department_name(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr){
        router.get()->http_get(R"(/user/department/name/:department([0-9]+))", [pool_ptr, logger_ptr](auto req, auto params) {
            auto qrl = req->header().path();

            std::string department = url::get_last_url_arg(qrl);
            logger_ptr->info( [department]{return fmt::format("department = {}", department);});

            if(department == "name" || department.empty()) {
                return req->create_response(restinio::status_bad_request()).done();
            }

            int department_id = std::stoi(department);

            std::string result = user::department_name(department_id, pool_ptr);
            
            if(result.empty()) {
                return req->create_response(restinio::status_bad_gateway()).done();
            }
            return req->create_response().set_body(result).done();
        });
    }

    void set_users_department(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_put("/user/set_department", [pool_ptr, logger_ptr](auto req, auto) {
            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());

            if (!new_body.HasMember("token") || !auth::is_admin(new_body["token"].GetString(), pool_ptr)) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            int result;
            if (new_body.HasMember("username") && new_body.HasMember("department")) {
                std::string department = new_body["department"].GetString();
                logger_ptr->info( [department]{return fmt::format("department = {}", department);});
                try {
                    if(new_body["department"].IsInt()) {
                        result = user::set_users_department(new_body["username"].GetString(), new_body["department"].GetInt(), pool_ptr);
                    } else {
                        result = user::set_users_department(new_body["username"].GetString(), new_body["department"].GetString(), pool_ptr);
                    }
                } catch (...) {
                    result = -1;
                }
                
                logger_ptr->info( [result]{return fmt::format("result = {}", result);});
                if (result == -1) {
                    return req->create_response(restinio::status_bad_request()).done();
                }
                return req->create_response().set_body("ok").done();
            } else {
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }
            return req->create_response(restinio::status_internal_server_error()).done();
        });
    }

    void all_departments(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get("/user/department/roles", [pool_ptr, logger_ptr](auto req, auto) {
            pqxx::result result = user::all_departments(pool_ptr);
            
            if(result.empty()) {
                return req->create_response(restinio::status_bad_gateway()).done();
            }
            return req->create_response().set_body(cp::serialize(result)).done();
        });
    }

    void starter_role(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/user/department/start/:department(.*))", [pool_ptr, logger_ptr](auto req, auto params) {
            auto qrl = req->header().path();

            std::string department = url::get_last_url_arg(qrl);
            logger_ptr->info( [department]{return fmt::format("department = {}", department);});

            if(department == "start" || department.empty()) {
                return req->create_response(restinio::status_bad_request()).done();
            }
            int result = -1;
            if(url::is_number(department)) {
                result = user::starter_role(std::stoi(department), pool_ptr);
            } else {
                result = user::starter_role(department, pool_ptr);
            }            
            if(result == -1) {
                return req->create_response(restinio::status_bad_gateway()).done();
            }
            return req->create_response().set_body(std::to_string(result)).done();
        });
    }
}