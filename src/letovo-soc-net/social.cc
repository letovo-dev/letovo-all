#include "social.h"
#include "../basic/analytics.h"

namespace social {

    bool is_leap_year(int year) {
        return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    }

    bool is_valid_news_date(const std::string& date) {
        if(date.size() != 10 || date[4] != '-' || date[7] != '-') {
            return false;
        }
        for(size_t i = 0; i < date.size(); ++i) {
            if(i == 4 || i == 7) {
                continue;
            }
            if(date[i] < '0' || date[i] > '9') {
                return false;
            }
        }

        int year = std::stoi(date.substr(0, 4));
        int month = std::stoi(date.substr(5, 2));
        int day = std::stoi(date.substr(8, 2));
        if(year < 1 || month < 1 || month > 12) {
            return false;
        }

        const int days_in_month[] = {31, is_leap_year(year) ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        return day >= 1 && day <= days_in_month[month - 1];
    }

    pqxx::result get_authors_list(std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        pqxx::result result = con->execute("SELECT \"username\", \"avatar_pic\", \"display_name\" from \"user\" where \"author\"=true;");
        pool_ptr->returnConnection(std::move(con));
        return result;
    }

    pqxx::result get_news(std::string start, int size, std::string username, std::optional<std::string> date, bool include_secret, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {username, start, std::to_string(size)};
        std::string query = "SELECT DISTINCT p.*, u.avatar_pic, u.display_name, case when l.username = ($1) and l.value = 1 then true else false end as is_liked, case when l.username = ($1) and l.value = -1 then true else false end as is_disliked, case when s.username is not null then true else false end as saved from \"posts\" p left join \"user_likes\" l on l.post_id = p.post_id AND l.username = $1 left join \"user_saved\" s on p.post_id = s.post_id and s.username = $1 left join \"user\" u on p.author = u.username WHERE p.parent_id is null and p.post_path is null";
        if(!include_secret) {
            query += " and p.is_secret = false";
        }
        if(date.has_value()) {
            params.push_back(*date);
            query += " and p.date >= ($4)::date and p.date < (($4)::date + interval '1 day')";
        }
        query += " ORDER BY p.date DESC offset ($2) LIMIT ($3);";
        pqxx::result result = con->execute_params(query, params);
        pool_ptr->returnConnection(std::move(con));
        return result;
    }

    pqxx::result get_comments(std::string post_id, std::string start, int size, std::string username, bool include_secret, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        // std::vector<std::string> params = {username, post_id, start, std::to_string(size)};
        std::vector<std::string> params = {username, start, std::to_string(size), post_id};
        // pqxx::result result = con->execute_params("SELECT p.*, case when l.username = ($1) and l.value = 1 then true else false end as is_liked, case when l.username = ($1) and l.value = -1 then true else false end as is_disliked from \"posts\" p left join \"user_likes\" l on l.post_id = p.post_id where p.parent_id is not null and p.parent_id = ($2) ORDER BY p.post_id DESC offset ($3) LIMIT ($4);", params);
        std::string query = "SELECT DISTINCT p.*, u.avatar_pic, u.display_name, case when l.username = ($1) and l.value = 1 then true else false end as is_liked, case when l.username = ($1) and l.value = -1 then true else false end as is_disliked, case when s.username is not null then true else false end as saved from \"posts\" p left join \"posts\" parent on p.parent_id = parent.post_id left join \"user_likes\" l on l.post_id = p.post_id AND l.username = $1 left join \"user_saved\" s on p.post_id = s.post_id and s.username = $1 left join \"user\" u on p.author = u.username WHERE p.parent_id = ($4) and p.post_path is null";
        if(!include_secret) {
            query += " and p.is_secret = false and COALESCE(parent.is_secret, false) = false";
        }
        query += " ORDER BY p.date DESC offset ($2) LIMIT ($3);";
        pqxx::result result = con->execute_params(query, params);
        pool_ptr->returnConnection(std::move(con));
        return result;
    }

    pqxx::result get_post_media(std::string post_id, bool pics, bool include_secret, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {post_id};
        pqxx::result result;
        std::string query = "SELECT post_media.* FROM \"post_media\" left join \"posts\" p on post_media.post_id = p.post_id::text WHERE \"post_media\".post_id=($1) and \"post_media\".is_secret=false";
        if(!include_secret) {
            query += " and COALESCE(p.is_secret, false) = false";
        }
        query += ";";
        result = con->execute_params(query, params);
        
        pool_ptr->returnConnection(std::move(con));
        return result;
    }

    // FIXME: bug select always returns empty result
    void add_like(int like, std::string post_id, std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {std::to_string(like), post_id, username};
        auto check = con->execute_params("SELECT * FROM \"user_likes\" ul WHERE ul.post_id=($2) AND ul.username=($3) and ul.value!=($1);", params);
        params = {post_id};
        if(check.size() > 0) {
            if(like == 1) {
                con->execute_params("UPDATE \"posts\" SET dislikes = dislikes - 1 WHERE post_id=($2);", params, true);
            } else if(like == -1) {
                con->execute_params("UPDATE \"posts\" SET likes = likes - 1 WHERE post_id=($2);", params, true);
            }
        }
        params = {post_id, username};
        con->execute_params("DELETE FROM \"user_likes\" WHERE post_id=($1) AND username=($2);", params, true);
        params = {std::to_string(like), post_id, username};
        con->execute_params("INSERT INTO \"user_likes\" (\"value\", \"post_id\", \"username\") VALUES ($1, $2, $3) ON CONFLICT (post_id, username) DO NOTHING;", params, true);
        params = {post_id};
        if(like == 1) {
            con->execute_params("UPDATE \"posts\" SET likes = likes + 1 WHERE post_id=($1);", params, true);
        } else if(like == -1) {
            con->execute_params("UPDATE \"posts\" SET dislikes = dislikes + 1 WHERE post_id=($1);", params, true);
        }
        pool_ptr->returnConnection(std::move(con));
    }

    void delete_like(int like, std::string post_id, std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {post_id, username};
        con->execute_params("DELETE FROM \"user_likes\" WHERE post_id=($1) AND username=($2);", params, true);
        params = {post_id};
        if(like == 1) {
            con->execute_params("UPDATE \"posts\" SET likes = likes - 1 WHERE post_id=($1);", params, true);
        } else if(like == -1) {
            con->execute_params("UPDATE \"posts\" SET dislikes = dislikes - 1 WHERE post_id=($1);", params, true);
        }
        pool_ptr->returnConnection(std::move(con));
    }


    int add_comment(std::string comment, std::string post_id, std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {comment, post_id, username};
        pqxx::result result = con->execute_params("INSERT INTO \"posts\" (\"text\", \"parent_id\", \"author\") VALUES ($1, $2, $3) returning \"post_id\";", params, true);
        pool_ptr->returnConnection(std::move(con));
        return result[0]["post_id"].as<int>();
    }

    pqxx::result get_all_titles(bool include_secret, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::string query = "SELECT post_id, title FROM \"posts\" where \"posts\".post_path is null and \"posts\".parent_id is null";
        if(!include_secret) {
            query += " and \"posts\".is_secret = false";
        }
        query += ";";
        pqxx::result result = con->execute(query);
        pool_ptr->returnConnection(std::move(con));
        return result;
    }

    pqxx::result get_all_posts(std::string username, bool include_secret, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {username};
        std::string query = "SELECT DISTINCT p.*, u.avatar_pic, u.display_name, case when l.username = ($1) and l.value = 1 then true else false end as is_liked, case when l.username = ($1) and l.value = -1 then true else false end as is_disliked, case when s.username is not null then true else false end as saved from \"posts\" p left join \"user_likes\" l on l.post_id = p.post_id AND l.username = $1 left join \"user_saved\" s on p.post_id = s.post_id and s.username = $1 left join \"user\" u on p.author = u.username WHERE p.parent_id is null and p.post_path is not null";
        if(!include_secret) {
            query += " and p.is_secret = false";
        }
        query += " ORDER BY p.date;";
        pqxx::result result = con->execute_params(query, params);
        pool_ptr->returnConnection(std::move(con));
        return result;
    }

    pqxx::result get_posts_by_author(std::string author, std::string username, bool include_secret, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {username, author};
        std::string query = "SELECT DISTINCT p.*, u.avatar_pic, u.display_name, case when l.username = ($1) and l.value = 1 then true else false end as is_liked, case when l.username = ($1) and l.value = -1 then true else false end as is_disliked, case when s.username is not null then true else false end as saved from \"posts\" p left join \"user_likes\" l on l.post_id = p.post_id AND l.username = $1 left join \"user_saved\" s on p.post_id = s.post_id and s.username = $1 left join \"user\" u on p.author = u.username WHERE p.parent_id is null and p.author = ($2)";
        if(!include_secret) {
            query += " and p.is_secret = false";
        }
        query += " ORDER BY p.date DESC;";
        pqxx::result result = con->execute_params(query, params);
        pool_ptr->returnConnection(std::move(con));
        return result;
    }

    pqxx::result get_saved_posts(std::string username, bool include_secret, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {username};
        std::string query = "SELECT p.*, u.avatar_pic, u.display_name, case when l.username = ($1) and l.value = 1 then true else false end as is_liked, case when l.username = ($1) and l.value = -1 then true else false end as is_disliked, case when s.username is not null then true else false end as saved from \"posts\" p left join \"user_likes\" l on l.post_id = p.post_id AND l.username = ($1) left join \"user_saved\" s on p.post_id = s.post_id and s.username = ($1) left join \"user\" u on p.author = u.username WHERE s.username=($1)";
        if(!include_secret) {
            query += " and p.is_secret = false";
        }
        query += ";";
        pqxx::result result = con->execute_params(query, params);
        pool_ptr->returnConnection(std::move(con));
        return result;
    }

    void save_post(std::string post_id, std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {post_id, username};
        con->execute_params("INSERT INTO \"user_saved\" (\"post_id\", \"username\") VALUES ($1, $2);", params, true);
        params = {post_id};
        con->execute_params("UPDATE \"posts\" SET saved_count = saved_count + 1 WHERE post_id=($1);", params, true);
        pool_ptr->returnConnection(std::move(con));
    }

    void delete_saved_post(std::string post_id, std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {post_id, username};
        con->execute_params("DELETE FROM \"user_saved\" WHERE post_id=($1) AND username=($2);", params, true);
        params = {post_id};
        con->execute_params("UPDATE \"posts\" SET saved_count = saved_count - 1 WHERE post_id=($1);", params, true);
        pool_ptr->returnConnection(std::move(con));
    }

    pqxx::result get_post(std::string post_id, std::string username, bool include_secret, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {username, post_id};
        std::string query = "SELECT DISTINCT p.*, u.avatar_pic, u.display_name, case when l.username = ($1) and l.value = 1 then true else false end as is_liked, case when l.username = ($1) and l.value = -1 then true else false end as is_disliked, case when s.username is not null then true else false end as saved from \"posts\" p left join \"user_likes\" l on l.post_id = p.post_id AND l.username = ($1) left join \"user_saved\" s on p.post_id = s.post_id and s.username = ($1) left join \"user\" u on p.author = u.username WHERE p.post_id = ($2)";
        if(!include_secret) {
            query += " and p.is_secret = false";
        }
        pqxx::result result = con->execute_params(query, params);
        pool_ptr->returnConnection(std::move(con));
        return result;
    }


    pqxx::result get_post_categories(std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());

        pqxx::result result = con->execute("select pc.category_id as category, pc.category_name from \"post_category\" pc;");

        pool_ptr->returnConnection(std::move(con));

        if (result.empty()) {
            return {};
        }
        return result;
    }

    pqxx::result get_post_by_category(std::string category, bool include_secret, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());

        std::vector<std::string> params = {category};

        std::string query = "select p.*, pc.category_name from \"posts\" p left join \"post_category\" pc on p.category = pc.category_id where p.category = ($1) AND p.post_path is not null";
        if (!include_secret) {
            query += " AND p.is_secret = false";
        }
        query += ";";

        pqxx::result result = con->execute_params(query, params);

        pool_ptr->returnConnection(std::move(con));

        if (result.empty()) {
            return {};
        }
        return result;
    }

}

namespace social::server {
    void get_authors_list(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get("/social/authors", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /social/authors";});
            pqxx::result result = social::get_authors_list(pool_ptr);
            return req->create_response()
                .set_body(cp::serialize(result))
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
    }

    void get_news(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/social/news:search(.*))", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /social/news";});
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            std::string username = auth::get_username(token, pool_ptr);
            if(username == "") {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            const auto qp = restinio::parse_query( req->header().query() );
            
            if(!qp.has("start") || !qp.has("size")) {
                return req->create_response(restinio::status_bad_request()).done();
            }
            std::optional<std::string> date;
            if(qp.has("date")) {
                date = (std::string)qp["date"];
                if(!social::is_valid_news_date(*date)) {
                    return req->create_response(restinio::status_bad_request()).done();
                }
            }
            const bool can_read_secret = security::can_read_secret_posts(username, pool_ptr);
            pqxx::result result = social::get_news((std::string)qp["start"], std::stoi((std::string)qp["size"]), username, date, can_read_secret, pool_ptr);
            analytics::record_event(username, token, req->remote_endpoint().address().to_string(),
                req->header().has_field("User-Agent") ? req->header().get_field("User-Agent") : "",
                "GET", "/social/news", 200, 0, "", "{}", pool_ptr, logger_ptr);
            return req->create_response()
                .set_body(cp::serialize_with_segment_day(result, pool_ptr))
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
    }

    void get_comments(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/social/comments:search(.*))", [pool_ptr, logger_ptr](auto req, auto) {            
            logger_ptr->trace([]{return "called /social/comments";});
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            std::string username = auth::get_username(token, pool_ptr);
            if(username == "") {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            const auto qp = restinio::parse_query( req->header().query() );
            if(!qp.has("post_id") || !qp.has("start") || !qp.has("size")) {
                return req->create_response(restinio::status_bad_request()).done();
            }
            const bool can_read_secret = security::can_read_secret_posts(username, pool_ptr);
            pqxx::result result = social::get_comments((std::string)qp["post_id"], (std::string)qp["start"], std::stoi((std::string)qp["size"]), username, can_read_secret, pool_ptr);
            analytics::record_event(username, token, req->remote_endpoint().address().to_string(),
                req->header().has_field("User-Agent") ? req->header().get_field("User-Agent") : "",
                "GET", "/social/comments", 200, 0, "", "{}", pool_ptr, logger_ptr);
            return req->create_response()
                .set_body(cp::serialize_with_shift_day(result, pool_ptr))
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
    }

    void get_post_pics(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/social/media/pics/:post_id(.*))", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /social/media/pics/:post_id";});
            std::string post_id = url::get_last_url_arg(req->header().path());
            bool pics = true; //req->header().get_field("pics") == "true";
            std::string token = security::bearer_or_cookie_token(req->header());
            std::string username = security::username_from_session(token, pool_ptr);
            const bool can_read_secret = security::can_read_secret_posts(username, pool_ptr);
            pqxx::result result = social::get_post_media(post_id, pics, can_read_secret, pool_ptr);
            return req->create_response()
                .set_body(cp::serialize(result))
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
    }

    void get_post_media(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/social/media/pics/:post_id(.*))", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /social/media/pics/:post_id";});
            std::string post_id = url::get_last_url_arg(req->header().path());
            bool pics = false; //req->header().get_field("pics") == "true";
            std::string token = security::bearer_or_cookie_token(req->header());
            std::string username = security::username_from_session(token, pool_ptr);
            const bool can_read_secret = security::can_read_secret_posts(username, pool_ptr);
            pqxx::result result = social::get_post_media(post_id, pics, can_read_secret, pool_ptr);
            return req->create_response()
                .set_body(cp::serialize(result))
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
    }

    void get_post(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/social/new/:post_id(.*))", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /social/new/:post_id";});
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            std::string username = auth::get_username(token, pool_ptr);
            if(username == "") {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            logger_ptr->info([username] { return fmt::format("user {} get post request", username); });
            std::string post_id = url::get_last_url_arg(req->header().path());
            logger_ptr->info([post_id] { return fmt::format("get post request for {}", post_id); });
            const bool can_read_secret = security::can_read_secret_posts(username, pool_ptr);
            pqxx::result result = social::get_post(post_id, username, can_read_secret, pool_ptr);
            return req->create_response()
                .set_body(cp::serialize_with_shift_day(result, pool_ptr))
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
    }

    void get_all_posts(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get("/social/posts", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /social/posts";});
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            std::string username = auth::get_username(token, pool_ptr);
            if(username == "") {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            const bool can_read_secret = security::can_read_secret_posts(username, pool_ptr);
            pqxx::result result = social::get_all_posts(username, can_read_secret, pool_ptr);
            return req->create_response()
                .set_body(cp::serialize_with_shift_day(result, pool_ptr))
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
    }

    void get_posts_by_author(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/social/posts/author/:username([a-zA-Z0-9_\-]+))", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /social/posts/author/:username";});
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            std::string username = auth::get_username(token, pool_ptr);
            if(username == "") {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            std::string author = url::get_last_url_arg(req->header().path());
            if(author.empty()) {
                return req->create_response(restinio::status_bad_request()).done();
            }
            const bool can_read_secret = security::can_read_secret_posts(username, pool_ptr);
            pqxx::result result = social::get_posts_by_author(author, username, can_read_secret, pool_ptr);
            return req->create_response()
                .set_body(cp::serialize_with_shift_day(result, pool_ptr))
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
    }

    void get_all_titles(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get("/social/titles", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /social/titles";});
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            std::string username = auth::get_username(token, pool_ptr);
            if(username == "") {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            const bool can_read_secret = security::can_read_secret_posts(username, pool_ptr);
            pqxx::result result = social::get_all_titles(can_read_secret, pool_ptr);
            analytics::record_event(username, token, req->remote_endpoint().address().to_string(),
                req->header().has_field("User-Agent") ? req->header().get_field("User-Agent") : "",
                "GET", "/social/titles", 200, 0, "", "{}", pool_ptr, logger_ptr);
            return req->create_response()
                .set_body(cp::serialize(result))
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
    }

    void get_saved_posts(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get("/social/saved", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /social/saved";});
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            std::string username = auth::get_username(token, pool_ptr);
            if(username == "") {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            const bool can_read_secret = security::can_read_secret_posts(username, pool_ptr);
            pqxx::result result = social::get_saved_posts(username, can_read_secret, pool_ptr);
            return req->create_response()
                .set_body(cp::serialize_with_shift_day(result, pool_ptr))
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
    }
    void save_post(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_post("/social/save", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /social/save";});
            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            std::string username = auth::get_username(token, pool_ptr);
            if(username == "") {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if(!new_body.HasMember("post_id")) {
                return req->create_response(restinio::status_bad_request()).done();
            }
            social::save_post(new_body["post_id"].GetString(), username, pool_ptr);
            return req->create_response().done();
        });
    }
    void delete_saved_post(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_delete("/social/save", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /social/save";});
            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            std::string username = auth::get_username(token, pool_ptr);
            if(username == "") {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if(!new_body.HasMember("post_id")) {
                return req->create_response(restinio::status_bad_request()).done();
            }
            social::delete_saved_post(new_body["post_id"].GetString(), username, pool_ptr);
            return req->create_response().done();
        });
    }

    void search_by_title(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/social/search:search(.*))", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /social/search";});
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            std::string username = auth::get_username(token, pool_ptr);
            if(username == "") {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            const auto qp = restinio::parse_query( req->header().query() );
            if(!qp.has("title")) {
                return req->create_response(restinio::status_bad_request()).done();
            }
            const bool can_read_secret = security::can_read_secret_posts(username, pool_ptr);
            pqxx::result result = social::get_post("111", username, can_read_secret, pool_ptr);
            return req->create_response()
                .set_body(cp::serialize_with_shift_day(result, pool_ptr))
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
    }

    void add_like(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_post("/social/like", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /social/like";});
            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            std::string username = auth::get_username(token, pool_ptr);
            if(username == "") {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if(!new_body.HasMember("post_id")) {
                return req->create_response(restinio::status_bad_request()).done();
            }
            try {
                social::add_like(1, new_body["post_id"].GetString(), username, pool_ptr);
            } catch (const std::exception& e) {
                return req->create_response(restinio::status_internal_server_error()).done();
            }
            return req->create_response().done();
        });
    }

    void add_dislike(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_post("/social/dislike", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /social/dislike";});
            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            std::string username = auth::get_username(token, pool_ptr);
            if(username == "") {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if(!new_body.HasMember("post_id")) {
                return req->create_response(restinio::status_bad_request()).done();
            }
            try {
                social::delete_like(1, new_body["post_id"].GetString(), username, pool_ptr);
                social::add_like(-1, new_body["post_id"].GetString(), username, pool_ptr);
            } catch (const std::exception& e) {
                return req->create_response(restinio::status_internal_server_error()).done();
            }
            return req->create_response().done();
        });
    }

    void delete_like(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_delete("/social/like", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /social/like";});
            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            std::string username = auth::get_username(token, pool_ptr);
            if(username == "") {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if(!new_body.HasMember("post_id")) {
                return req->create_response(restinio::status_bad_request()).done();
            }
            try {
                social::delete_like(1, new_body["post_id"].GetString(), username, pool_ptr);
            } catch (const std::exception& e) {
                return req->create_response(restinio::status_internal_server_error()).done();
            }
            return req->create_response().done();
        });
    }

    void delete_dislike(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_delete("/social/dislike", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /social/dislike";});
            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            std::string username = auth::get_username(token, pool_ptr);
            if(username == "") {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if(!new_body.HasMember("post_id")) {
                return req->create_response(restinio::status_bad_request()).done();
            }
            try {
                social::delete_like(-1, new_body["post_id"].GetString(), username, pool_ptr);
            } catch (const std::exception& e) {
                return req->create_response(restinio::status_internal_server_error()).done();
            }
            return req->create_response().done();
        });
    }

    void add_comment(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_post("/social/comments", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /social/comments";});
            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            std::string username = auth::get_username(token, pool_ptr);
            if(username == "") {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            const std::string actor = username;
            if(!new_body.HasMember("post_id") || !new_body.HasMember("comment")) {
                return req->create_response(restinio::status_bad_request()).done();
            }
            if(new_body.HasMember("author") && authors::check_if_avaluable_author(username, new_body["author"].GetString(), pool_ptr)) {
                username = new_body["author"].GetString();
            }
            try {
                std::string comment = new_body["comment"].GetString();
                assist::fix_new_lines(comment);
                int post_id = social::add_comment(comment, new_body["post_id"].GetString(), username, pool_ptr);
                const bool can_read_secret = security::can_read_secret_posts(username, pool_ptr);
                pqxx::result result = social::get_post(std::to_string(post_id), username, can_read_secret, pool_ptr);
                analytics::record_event(actor, token, req->remote_endpoint().address().to_string(),
                    req->header().has_field("User-Agent") ? req->header().get_field("User-Agent") : "",
                    "POST", "/social/comments", 200, 0, "", "{}", pool_ptr, logger_ptr);
                return req->create_response()
                    .set_body(cp::serialize_with_shift_day(result, pool_ptr))
                    .append_header("Content-Type", "application/json; charset=utf-8")
                    .done();
            } catch(const std::exception& e) {
                logger_ptr->error([e] { return fmt::format("Error: {}", e.what()); });
                return req->create_response(restinio::status_internal_server_error()).done();
            }
        });
    }


    void get_post_categories(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get("/social/categories", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /social/categories";});
            pqxx::result result = social::get_post_categories(pool_ptr);
            if (result.empty()) {
                return req->create_response(restinio::status_bad_gateway()).done();
            }
            return req->create_response().set_body(cp::serialize(result))
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
    }

    void get_post_by_category(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/social/bycat/:category([0-9\-]+))", [pool_ptr, logger_ptr](auto req, auto params) {
            logger_ptr->trace([]{return "called /social/bycat/:category";});
            auto qrl = req->header().path();

            std::string category = url::get_last_url_arg(qrl);

            logger_ptr->info([category] { return fmt::format("get post by category request for {}", category); });

            if (category == "category" || category.empty()) {
                return req->create_response(restinio::status_bad_request()).done();
            }
            const std::string token = security::bearer_or_cookie_token(req->header());
            const std::string actor = security::username_from_session(token, pool_ptr);
            const bool can_read_secret = !actor.empty() && security::can_read_secret_posts(actor, pool_ptr);
            if (category == "5" && !can_read_secret) {
                return req->create_response(restinio::status_forbidden()).done();
            }
            pqxx::result result;
            try {
                result = social::get_post_by_category(category, can_read_secret, pool_ptr);
            } catch (const std::exception& e) {
                logger_ptr->error([e] { return fmt::format("Error: {}", e.what()); });
                return req->create_response(restinio::status_internal_server_error()).done();
            }

            if (result.empty()) {
                return req->create_response(restinio::status_bad_gateway()).done();
            }
            return req->create_response().set_body(cp::serialize_with_shift_day(result, pool_ptr))
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
    }
}
