#include "achivements.h"

namespace achivements {
    pqxx::result full_user_achivements(std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());

        std::vector<std::string> params = {username};

        pqxx::result result = con->execute_params("select *, (\"user_achivements\".username IS NOT NULL AND \"user_achivements\".stage >= \"achivements\".stages) as completed from \"user_achivements\" right join \"achivements\" on \"user_achivements\".achivement_id = \"achivements\".achivement_id where \"user_achivements\".username = ($1) or \"user_achivements\".username is null order by \"achivement_tree\" asc, \"level\" desc;", params);

        pool_ptr->returnConnection(std::move(con));

        if (result.empty()) {
            return {};
        }
        return result;
    }

    pqxx::result user_achivements(std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());

        std::vector<std::string> params = {username};

        pqxx::result result = con->execute_params("select *, (\"user_achivements\".stage >= \"achivements\".stages) as completed from \"user_achivements\" right join \"achivements\" on \"user_achivements\".achivement_id = \"achivements\".achivement_id where \"user_achivements\".username = ($1) order by \"achivement_tree\" asc, \"level\" desc;", params);

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
        auto con = std::move(pool_ptr->getConnection());

        std::vector<std::string> params = {name, std::to_string(tree_id), std::to_string(level), pic, description, std::to_string(stages)};

        pqxx::result id = {};
        try {
            id = con->execute_params("INSERT INTO \"achivements\" (achivement_name, achivement_tree, level, achivement_pic, achivement_decsription, stages) VALUES ($1, $2, $3, $4, $5, $6) RETURNING achivement_id;", params, true);
        } catch (...) {
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

    pqxx::result department_achivements(std::string username, std::string department_id, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());

        std::vector<std::string> params = {username, department_id};

        pqxx::result result = con->execute_params("select ua.id, ua.username, ua.achivement_id, ua.datetime, ua.stage as level, ach.achivement_id, ach.achivement_pic, ach.achivement_name, ach.achivement_decsription, ach.achivement_tree, ach.stages, ach.category, ach.category_name, ach.departmentid, (ua.username IS NOT NULL AND ua.stage >= ach.stages) as completed from \"user_achivements\" ua right join \"achivements\" ach on ua.achivement_id = ach.achivement_id and ua.username = ($1) where ach.departmentid = ($2) order by \"achivement_tree\" asc--, \"level\" desc;", params);

        pool_ptr->returnConnection(std::move(con));

        if (result.empty()) {
            return {};
        }
        return result;
    }

    std::string user_achivements_by_department_json(std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());

        std::vector<std::string> params = {username};

        pqxx::result result = con->execute_params(
            R"(SELECT json_build_object(
    'username', $1::text,
    'completed_count', (
        SELECT COUNT(*) FROM "user_achivements" ua
        INNER JOIN "achivements" ach ON ua.achivement_id = ach.achivement_id
        WHERE ua.username = $1::text AND ua.stage >= ach.stages
          AND ach.departmentid IS NOT NULL AND ach.departmentid <> -1
    ),
    'not_completed_count', (
        SELECT COUNT(*) FROM "user_achivements" ua
        INNER JOIN "achivements" ach ON ua.achivement_id = ach.achivement_id
        WHERE ua.username = $1::text AND ua.stage < ach.stages
          AND ach.departmentid IS NOT NULL AND ach.departmentid <> -1
    ),
    'achivements', COALESCE(
        (
            SELECT json_agg(
                json_build_object(
                    'year', x.year_val,
                    'chapter', x.chapter_val,
                    'department_id', x.departmentid,
                    'department', x.departmentname,
                    'role', x.rolename,
                    'post', COALESCE(x.post_json, '[]'::json),
                    'brigade', COALESCE(x.brigade_json, '[]'::json),
                    'achivements', x.json_data
                )
                ORDER BY x.year_val, x.chapter_val, x.departmentid
            )
            FROM (
                SELECT
                    grp.year_val,
                    grp.chapter_val,
                    grp.departmentid,
                    grp.departmentname,
                    grp.rolename,
                    grp.json_data,
                    (
                        SELECT COALESCE(json_agg(sub.pn ORDER BY sub.pn), '[]'::json)
                        FROM (
                            SELECT DISTINCT po.post_name AS pn
                            FROM "user_posts" upo
                            INNER JOIN "post" po ON po.post_id = upo.post_id
                            WHERE upo.username = $1::text
                              AND COALESCE(EXTRACT(YEAR FROM upo.datetime)::int, 0) = grp.year_val
                              AND COALESCE(
                                  (
                                      SELECT c.chapter
                                      FROM "calendar" c
                                      WHERE upo.datetime IS NOT NULL
                                        AND upo.datetime >= c.start
                                        AND upo.datetime <= c."end"
                                      ORDER BY c.start
                                      LIMIT 1
                                  ),
                                  ''::text
                              ) = grp.chapter_val
                        ) sub
                        WHERE sub.pn IS NOT NULL
                    ) AS post_json,
                    (
                        SELECT COALESCE(json_agg(sub.bn ORDER BY sub.bn), '[]'::json)
                        FROM (
                            SELECT DISTINCT b.name AS bn
                            FROM "user_brigades" ubr
                            INNER JOIN "brigade" b ON b.brigade_id = ubr.brigade_id
                            WHERE ubr.username = $1::text
                              AND COALESCE(EXTRACT(YEAR FROM ubr.datetime)::int, 0) = grp.year_val
                              AND COALESCE(
                                  (
                                      SELECT c.chapter
                                      FROM "calendar" c
                                      WHERE ubr.datetime IS NOT NULL
                                        AND ubr.datetime >= c.start
                                        AND ubr.datetime <= c."end"
                                      ORDER BY c.start
                                      LIMIT 1
                                  ),
                                  ''::text
                              ) = grp.chapter_val
                        ) sub
                        WHERE sub.bn IS NOT NULL
                    ) AS brigade_json
                FROM (
                    SELECT
                        COALESCE(EXTRACT(YEAR FROM ua.datetime)::int, 0) AS year_val,
                        COALESCE(cal.chapter, ''::text) AS chapter_val,
                        d.departmentid,
                        d.departmentname,
                        COALESCE(br.rolename, ''::text) AS rolename,
                        COALESCE(
                            json_agg(
                                json_build_object(
                                    'id', ua.id,
                                    'new', COALESCE(ua."new", false),
                                    'datetime', ua.datetime,
                                    'stage', ua.stage,
                                    'achivement_pic', ach.achivement_pic,
                                    'achivement_name', ach.achivement_name,
                                    'achivement_decsription', ach.achivement_decsription,
                                    'stages', ach.stages,
                                    'category', ach.category,
                                    'category_name', ach.category_name,
                                    'departmentid', ach.departmentid,
                                    'completed', ua.stage >= ach.stages
                                )
                                ORDER BY ach.achivement_tree ASC, ach.level DESC NULLS LAST
                            ),
                            '[]'::json
                        ) AS json_data
                    FROM "user_achivements" ua
                    INNER JOIN "achivements" ach ON ua.achivement_id = ach.achivement_id
                    INNER JOIN "department" d ON d.departmentid = ach.departmentid
                    LEFT JOIN LATERAL (
                        SELECT c.chapter
                        FROM "calendar" c
                        WHERE ua.datetime IS NOT NULL
                          AND ua.datetime >= c.start
                          AND ua.datetime <= c."end"
                        ORDER BY c.start
                        LIMIT 1
                    ) cal ON true
                    LEFT JOIN LATERAL (
                        SELECT r.rolename
                        FROM "useroles" uo
                        INNER JOIN "roles" r ON uo.roleid = r.roleid
                        WHERE uo.username = $1::text
                          AND r.departmentid = ach.departmentid
                        ORDER BY r.rang DESC NULLS LAST
                        LIMIT 1
                    ) br ON true
                    WHERE ua.username = $1::text
                      AND ach.departmentid IS NOT NULL
                      AND ach.departmentid <> -1
                    GROUP BY
                        COALESCE(EXTRACT(YEAR FROM ua.datetime)::int, 0),
                        COALESCE(cal.chapter, ''::text),
                        d.departmentid,
                        d.departmentname,
                        COALESCE(br.rolename, ''::text)
                ) grp
            ) x
        ),
        '[]'::json
    )
)::text)",
            params);

        pool_ptr->returnConnection(std::move(con));

        if (result.empty() || result[0][0].is_null()) {
            return std::string("{\"username\":\"") + username + "\",\"achivements\":[]}";
        }
        return result[0][0].as<std::string>();
    }
    std::string current_segment_day(std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());

        pqxx::result result = con->execute(
            R"(SELECT chapter, start, "end", (CURRENT_DATE - start::date) + 1 AS day_of_segment
               FROM "calendar"
               WHERE NOW() >= start AND NOW() <= "end"
               ORDER BY start
               LIMIT 1;)");

        pool_ptr->returnConnection(std::move(con));

        if (result.empty()) {
            return "";
        }

        auto row = result[0];
        return fmt::format(
            R"({{"chapter":"{}","day":{},"start":"{}","end":"{}"}})",
            row["chapter"].as<std::string>(),
            row["day_of_segment"].as<int>(),
            row["start"].as<std::string>(),
            row["end"].as<std::string>()
        );
    }
} // namespace achivements

namespace achivements::server {
    void user_achivements_by_department(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/achivements/user/departments/:username([a-zA-Z0-9\-]+))", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([] { return "called /achivements/user/departments/:username"; });
            auto qrl = req->header().path();

            std::string username = url::get_last_url_arg(qrl);

            if (username == "departments" || username.empty()) {
                return req->create_response(restinio::status_bad_request()).done();
            }

            std::string body;
            try {
                body = achivements::user_achivements_by_department_json(username, pool_ptr);
            } catch (...) {
                return req->create_response(restinio::status_internal_server_error()).done();
            }

            return req->create_response().set_body(body)
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
    }

    void user_achivemets(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/achivements/user/:username([a-zA-Z0-9\-]+))", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /achivements/user/:username";});
            auto qrl = req->header().path();

            std::string username = url::get_last_url_arg(qrl);

            if (username == "user" || username.empty()) {
                return req->create_response(restinio::status_bad_request()).done();
            }
            
            pqxx::result result = achivements::user_achivements(username, pool_ptr);

            if (result.empty()) {
                return req->create_response(restinio::status_bad_gateway()).done();
            }
            return req->create_response().set_body(cp::serialize_with_segment_day(result, pool_ptr, "completed"))
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
    }

    void full_user_achivemets(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/achivements/user/full/:username([a-zA-Z0-9\-]+))", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /achivements/user/full/:username";});
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
            return req->create_response().set_body(cp::serialize_with_segment_day(result, pool_ptr, "completed"))
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
    }

    void add_achivement(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_post("/achivements/add", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /achivements/add";});
            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());

            if (!new_body.HasMember("username") || !new_body.HasMember("achivement_id")) {
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }

            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }

            if (token.empty()) {
                logger_ptr->info([]{return "token is empty";});
                return req->create_response(restinio::status_unauthorized()).done();
            }

            if (!auth::is_admin(token, pool_ptr) && !auth::is_rights_by_username(auth::get_username(token, pool_ptr), pool_ptr, "moder")) {
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
            logger_ptr->trace([]{return "called /achivements/delete";});
            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());

            if (!new_body.HasMember("username") || !new_body.HasMember("achivement_id")) {
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }

            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
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
            logger_ptr->trace([]{return "called /achivements/tree/:tree_id";});
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
            logger_ptr->trace([]{return "called /achivements/info/:achivement_id";});
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
            logger_ptr->trace([]{return "called /achivements/create";});
            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());

            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
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
                result = achivements::create_achivement(name, tree_id, level, pic, description, stages, pool_ptr);
                if (result == -1) {
                    return req->create_response(restinio::status_bad_request()).done();
                }
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
            logger_ptr->trace([]{return "called /achivements/pictures";});
            return req->create_response().set_body(cp::serialize(achivements::achivement_pictures()))
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
    }

    void no_department_achivements(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/achivements/no_dep)", [pool_ptr, logger_ptr](auto req, auto params) {
            logger_ptr->trace([]{return "called /achivements/no_dep";});
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if (token.empty()) {
                logger_ptr->info([]{return "token is empty";});
                return req->create_response(restinio::status_unauthorized()).done();
            }
            std::string username = auth::get_username(token, pool_ptr);
            pqxx::result result = achivements::department_achivements(username, "-1", pool_ptr);

            return req->create_response().set_body(cp::serialize_with_segment_day(result, pool_ptr, "completed"))
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
    }

    void department_achivements_by_user(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/achivements/by_user)", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /achivements/by_user";});
            auto qrl = req->header().path();

            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if (token.empty()) {
                logger_ptr->info([]{return "token is empty";});
                return req->create_response(restinio::status_unauthorized()).done();
            }
            std::string username = auth::get_username(token, pool_ptr);

            pqxx::result result = user::full_user_info(username, pool_ptr);

            if (result.empty()) {
                return req->create_response(restinio::status_bad_gateway()).done();
            }

            result = achivements::department_achivements(username, result[0]["departmentid"].as<std::string>(), pool_ptr);

            return req->create_response().set_body(cp::serialize_with_segment_day(result, pool_ptr, "completed"))
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
            });
    }
    void qr_code_by_achivement(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/achivements/qr_code/:achivement_id([0-9]+))", [pool_ptr, logger_ptr](auto req, auto params) {
            logger_ptr->trace([]{return "called /achivements/qr_code/:achivement_id";});
            auto qrl = req->header().path();

            std::string achivement_id = url::get_last_url_arg(qrl);

            if (achivement_id == "qr_code" || achivement_id.empty()) {
                return req->create_response(restinio::status_bad_request()).done();
            }

            logger_ptr ->info([achivement_id] { return fmt::format("achivement_id = {}", achivement_id); });

            int id = std::stoi(achivement_id);

            // here we should get a qr_path from database and send a file by that path

            std::string relative_filename = "images/uploaded/temp_qr.png";
            
            std::string file_path = media::check_if_file_exists(relative_filename);

            return req->create_response()
                .append_header(restinio::http_field::content_type, "image/png; charset=utf-8")
                .set_body(restinio::sendfile(file_path))
                .done();
        });
    }
    void calendar_day(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get("/calendar/day", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([] { return "called /calendar/day"; });

            std::string body;
            try {
                body = achivements::current_segment_day(pool_ptr);
            } catch (...) {
                return req->create_response(restinio::status_internal_server_error()).done();
            }

            if (body.empty()) {
                return req->create_response(restinio::status_not_found()).done();
            }

            return req->create_response()
                .set_body(body)
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
    }
} // namespace achivements::server
