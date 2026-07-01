#include "page_server.h"
#include "../basic/security.h"

#include <cerrno>
#include <cstdlib>
#include <limits>
#include <optional>

namespace {

    bool parse_int_string(const char* value, int& out) {
        if (value == nullptr || value[0] == '\0') {
            return false;
        }

        char* end = nullptr;
        errno = 0;
        long parsed = std::strtol(value, &end, 10);
        if (errno != 0 || end == value || *end != '\0') {
            return false;
        }
        if (parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) {
            return false;
        }

        out = static_cast<int>(parsed);
        return true;
    }

    std::optional<int> json_int_value(const rapidjson::Value& value) {
        if (value.IsInt()) {
            return value.GetInt();
        }
        if (value.IsString()) {
            int parsed = 0;
            if (parse_int_string(value.GetString(), parsed)) {
                return parsed;
            }
        }
        return std::nullopt;
    }

    std::optional<int> json_int_member(const rapidjson::Document& body, const char* key) {
        if (!body.HasMember(key)) {
            return std::nullopt;
        }
        return json_int_value(body[key]);
    }

    std::optional<bool> json_bool_value(const rapidjson::Value& value) {
        if (value.IsBool()) {
            return value.GetBool();
        }
        if (value.IsString()) {
            std::string str = value.GetString();
            if (str == "t" || str == "true" || str == "1") {
                return true;
            }
            if (str == "f" || str == "false" || str == "0") {
                return false;
            }
        }
        return std::nullopt;
    }

    std::optional<std::string> json_string_member(const rapidjson::Document& body, const char* key) {
        if (!body.HasMember(key)) {
            return std::nullopt;
        }
        if (body[key].IsNull()) {
            return std::nullopt;
        }
        if (!body[key].IsString()) {
            return std::nullopt;
        }
        return std::string(body[key].GetString());
    }

    bool json_string_member_has_invalid_type(const rapidjson::Document& body, const char* key) {
        return body.HasMember(key) && !body[key].IsString() && !body[key].IsNull();
    }

    std::string row_string_or_empty(const pqxx::row& row, const char* key) {
        for (const auto& field : row) {
            if (std::string(field.name()) == key) {
                return field.is_null() ? "" : field.as<std::string>();
            }
        }
        return "";
    }

    int row_int_or_zero(const pqxx::row& row, const char* key) {
        for (const auto& field : row) {
            if (std::string(field.name()) == key) {
                return field.is_null() ? 0 : field.as<int>();
            }
        }
        return 0;
    }

    bool row_bool_or_default(const pqxx::row& row, const char* key, bool default_value) {
        for (const auto& field : row) {
            if (std::string(field.name()) == key) {
                return field.is_null() ? default_value : field.as<bool>();
            }
        }
        return default_value;
    }

    bool row_bool_or_false(const pqxx::row& row, const char* key) {
        return row_bool_or_default(row, key, false);
    }

    void normalize_article_categories(std::unique_ptr<cp::AsyncConnection>& con) {
        con->execute(R"SQL(
            UPDATE "posts"
            SET "post_path" = NULL
            WHERE "post_path" = '';

            WITH article_categories AS (
                SELECT DISTINCT btrim("category_name") AS "category_name"
                FROM "posts"
                WHERE "post_path" IS NOT NULL
                    AND "category_name" IS NOT NULL
                    AND btrim("category_name") <> ''
            ),
            inserted_categories AS (
                INSERT INTO "post_category" ("category_name")
                SELECT "category_name"
                FROM article_categories
                ON CONFLICT ("category_name") DO NOTHING
                RETURNING "category_id", "category_name"
            ),
            available_categories AS (
                SELECT "category_id", "category_name"
                FROM inserted_categories

                UNION

                SELECT pc."category_id", pc."category_name"
                FROM "post_category" pc
                JOIN article_categories ac ON ac."category_name" = pc."category_name"
            )
            UPDATE "posts" p
            SET "category" = ac."category_id",
                "category_name" = ac."category_name"
            FROM available_categories ac
            WHERE p."post_path" IS NOT NULL
                AND p."category_name" IS NOT NULL
                AND btrim(p."category_name") = ac."category_name"
                AND (
                    p."category" IS DISTINCT FROM ac."category_id"
                    OR p."category_name" IS DISTINCT FROM ac."category_name"
                );
        )SQL", true);
    }

}

namespace page {

    pqxx::result get_page_content(int post_id, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<int> params = {post_id};
        pqxx::result result = con->execute_params("SELECT * FROM \"posts\" WHERE \"post_id\"=($1);", params);
        pool_ptr->returnConnection(std::move(con));
        return result;
    }

    pqxx::result get_page_author(std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {username};
        pqxx::result result = con->execute_params("SELECT \"username\", \"avatar_pic\", \"display_name\" from \"user\" WHERE \"username\"=($1);", params);
        pool_ptr->returnConnection(std::move(con));
        return result;
    }

    int add_page_by_content(bool is_secret, int likes, int dislikes, int saved, std::string title, std::string author, std::string text, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {std::to_string(is_secret), std::to_string(likes), std::to_string(dislikes), std::to_string(saved), title, author, text};
        pqxx::result result = con->execute_params("INSERT INTO \"posts\" (\"is_secret\", \"likes\", \"dislikes\", \"saved_count\", \"title\", \"author\", \"text\") VALUES ($1, $2, $3, $4, $5, $6, $7) RETURNING \"post_id\";", params, true);
        pool_ptr->returnConnection(std::move(con));
        return result[0]["post_id"].as<int>();
    }

    int add_page_by_page(std::string post_path, std::string category, std::string title, bool is_secret, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {post_path, category, title, std::to_string(is_secret)};
        pqxx::result result = con->execute_params("INSERT INTO \"posts\" (\"post_path\", \"category_name\", \"title\", \"is_secret\") VALUES ($1, $2, $3, $4) RETURNING \"post_id\";", params, true);
        normalize_article_categories(con);
        pool_ptr->returnConnection(std::move(con));
        return result[0]["post_id"].as<int>();
    }

    void update_likes(int post_id, int likes, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {std::to_string(likes), std::to_string(post_id)};
        con->execute_params("UPDATE \"posts\" SET \"likes\"=($1) WHERE \"post_id\"=($2);", params, true);
        pool_ptr->returnConnection(std::move(con));
    }

    pqxx::result get_favourite_posts(std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {username};
        pqxx::result result = con->execute_params("SELECT username, post_id from \"favourite_posts\" WHERE \"username\"=($1);", params);
        pool_ptr->returnConnection(std::move(con));
        return result;
    }

    pqxx::result add_favourite_post(int post_id, std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {std::to_string(post_id), username};
        pqxx::result result = con->execute_params("INSERT INTO \"favourite_posts\" (\"post_id\", \"username\") VALUES ($1, $2) ON CONFLICT (\"post_id\", \"username\") DO NOTHING;", params, true);
        pool_ptr->returnConnection(std::move(con));
        return result;
    }

    void delete_favourite_post(int post_id, std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {std::to_string(post_id), username};
        con->execute_params("DELETE FROM \"favourite_posts\" WHERE \"post_id\"=($1) AND \"username\"=($2);", params, true);
        pool_ptr->returnConnection(std::move(con));
    }

    void rename_category(std::string old_name, std::string new_name, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {old_name, new_name};
        con->execute_params("UPDATE \"posts\" SET \"category_name\"=($2) WHERE \"category_name\"=($1);", params, true);
        pool_ptr->returnConnection(std::move(con));
    }

    void delete_post(int post_id, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {std::to_string(post_id)};
        con->execute_params("DELETE FROM \"posts\" WHERE \"post_id\"=($1);", params, true);
        pool_ptr->returnConnection(std::move(con));
    }
    void update_post(int post_id, bool is_secret, int likes, int dislikes, int saved_count, std::string title, std::string author, std::string text, std::string category, std::string post_path, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {std::to_string(is_secret), std::to_string(likes), std::to_string(dislikes), std::to_string(saved_count), title, author, text, category, post_path, std::to_string(post_id)};

        con->execute_params("UPDATE \"posts\" SET \"is_secret\"=($1), \"likes\"=($2), \"dislikes\"=($3), \"saved_count\"=($4), \"title\"=($5), \"author\"=($6), \"text\"=($7), \"category_name\"=($8), \"post_path\"=($9) WHERE \"post_id\"=($10);", params, true);
        normalize_article_categories(con);
        pool_ptr->returnConnection(std::move(con));
    }

    void add_media(int post_id, std::vector<std::string> &media_paths, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params(2);
        for (const auto& media_path : media_paths) {
            params[0] = std::to_string(post_id);
            params[1] = media_path;
            // could commit after cycle, but idk how
            con->execute_params("INSERT INTO \"post_media\" (\"post_id\", \"media\") VALUES ($1, $2);", params, true);
        }
        pool_ptr->returnConnection(std::move(con));
    }

    void delete_media(int post_id, std::vector<std::string> &media_paths, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params(2);
        for (const auto& media_path : media_paths) {
            params[0] = std::to_string(post_id);
            params[1] = media_path;
            try {
                con->execute_params("DELETE FROM \"post_media\" WHERE \"post_id\"=($1) AND \"media\"=($2);", params, true);
            } catch (const std::exception& e) {
                logger_ptr->error( [e]{return fmt::format("error deleting media: {}", e.what());});
            }
        }
        pool_ptr->returnConnection(std::move(con));
    }

    void med_to_vec(const rapidjson::Document& new_body, std::vector<std::string>& out_media) {
        if (new_body.HasMember("media") && new_body["media"].IsArray()) {
            const rapidjson::Value& mediaArray = new_body["media"];
            for (auto& v : mediaArray.GetArray()) {
                if (v.IsString()) {
                    out_media.emplace_back(v.GetString());
                } 
            }
        }
    }

    void reveal_secret_page(int post_id, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<int> params = {post_id};
        con->execute_params("UPDATE \"posts\" SET \"is_secret\"=false WHERE \"post_id\"=($1);", params, true);
        pool_ptr->returnConnection(std::move(con));
    }

    std::string reveal_secret_url(const std::string& token) {
        return fmt::format("https://{}/api/post/reveal_secret/{}",
            Config::giveMe().server_config.adress, token);
    }
}


namespace page::server {
    void get_page_content(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/post/:id(\d+))", [pool_ptr, logger_ptr](auto req, auto params) {
            logger_ptr->trace([]{return "called /post/:id";});
            std::string endpoint = req->remote_endpoint().address().to_string();
            auto qrl = req->header().path();

            int postid = url::last_int_from_url_path(qrl);

            if (postid <= 0) {
                return req->create_response(restinio::status_bad_request()).done();
            }

            pqxx::result result = page::get_page_content(postid, pool_ptr);
            if(result.empty()) {
                return req->create_response(restinio::status_bad_gateway()).done();
            }

            if (row_bool_or_false(result[0], "is_secret") || !row_bool_or_default(result[0], "is_published", true)) {
                logger_ptr->info( [endpoint]{return fmt::format("page request from {} is secret", endpoint);});
                return req->create_response(restinio::status_not_found()).done();
            }

            return req->create_response()
                .set_body(cp::serialize_with_shift_day(result, pool_ptr))
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
            
        });
    }

    void get_page_author(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/post/author/:username([a-zA-Z0-9\-]+))", [pool_ptr, logger_ptr](auto req, auto params) {
            logger_ptr->trace([]{return "called /post/author/:username";});
            std::string endpoint = req->remote_endpoint().address().to_string();
            auto qrl = req->header().path();

            std::string username = url::get_last_url_arg(qrl);

            if(username == "author" || username.empty()) {
                return req->create_response(restinio::status_bad_request()).done();
            }

            pqxx::result result = page::get_page_author(username, pool_ptr);
            
            if(result.empty()) {
                return req->create_response(restinio::status_bad_gateway()).done();
            }
            return req->create_response()
                .append_header("Content-Type", "application/json; charset=utf-8")
                .set_body(cp::serialize(result))
                .done();
            
        });
    }

    void add_page(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_post("/post/add_page", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /post/add_page";});
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                logger_ptr->info( []{return "token is empty";});
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if (token.empty()) {
                logger_ptr->info( []{return "token is empty";});
                return req->create_response(restinio::status_unauthorized()).done();
            }
            
            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());
            int post_id;
            std::string title;
            if(new_body.HasMember("title")) {
                title = new_body["title"].GetString();
                assist::fix_new_lines(title);
            } else {
                title = "";
            }
            if(new_body.HasMember("post_path")) {
                if(!auth::is_admin(token, pool_ptr)) {
                    logger_ptr->info( []{return "not admin";});
                    return req->create_response(restinio::status_unauthorized()).done();
                }
                logger_ptr->info( []{return "add new wiki page";});
                post_id = page::add_page_by_page(
                    new_body["post_path"].GetString(),
                    new_body.HasMember("category_name") ? new_body["category_name"].GetString() : "",
                    title,
                    new_body.HasMember("is_secret") ? (new_body["is_secret"].GetString()[0] == 't') : false,
                    pool_ptr, logger_ptr
                );
            } else {
                logger_ptr->info( []{return "add new news post";});
                std::string author;
                if(new_body.HasMember("author")) {
                    author = new_body["author"].GetString();
                } else {
                    author = auth::get_username(token, pool_ptr);
                }
                assist::fix_new_lines(author);
                if(!authors::check_if_avaluable_author(auth::get_username(token, pool_ptr), author, pool_ptr)) {
                    logger_ptr->info( []{return "bad author";});
                    return req->create_response(restinio::status_non_authoritative_information()).done();
                }
                if(!new_body.HasMember("title") || !new_body.HasMember("text")) {
                    logger_ptr->info( []{return "bad request";});
                    return req->create_response(restinio::status_bad_request()).done();
                }
                std::string text = new_body["text"].GetString();
                std::string title = new_body["title"].GetString();
                assist::fix_new_lines(text);
                assist::fix_new_lines(title);
                post_id = page::add_page_by_content(
                    new_body.HasMember("is_secret") ? (new_body["is_secret"].GetString()[0] == 't') : false,
                    new_body.HasMember("likes") ? stoi(new_body["likes"].GetString()) : 0,
                    new_body.HasMember("dislikes") ? stoi(new_body["dislikes"].GetString()) : 0,
                    new_body.HasMember("saved") ? stoi(new_body["saved"].GetString()) : 0,
                    title,
                    new_body.HasMember("author") ? new_body["author"].GetString() : auth::get_username(token, pool_ptr),
                    text,
                    pool_ptr, logger_ptr
    
                );

                std::vector<std::string> media_paths;
                page::med_to_vec(new_body, media_paths);
                if (!media_paths.empty()) {
                    page::add_media(post_id, media_paths, pool_ptr, logger_ptr);
                }
            }

            return req->create_response(restinio::status_ok())
                .append_header("Content-Type", "application/json; charset=utf-8")
                .set_body(cp::serialize_with_shift_day(page::get_page_content(post_id, pool_ptr), pool_ptr))
                .done();

        });
    }

    void update_likes(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_put("/post/update_likes", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /post/update_likes";});
            if(!auth::is_authed_by_body(req->body(), pool_ptr)) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            std::string endpoint = req->remote_endpoint().address().to_string();

            logger_ptr->info( [endpoint]{return fmt::format("page request from {}", endpoint);});

            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());

            if (new_body.HasMember("post_id") && new_body.HasMember("likes")) {
                page::update_likes(new_body["post_id"].GetInt(), new_body["likes"].GetInt(), pool_ptr, logger_ptr);
                logger_ptr->info( [endpoint, likes = new_body["likes"].GetInt()]{return fmt::format("likes updated to {} from {}", likes, endpoint);});
                return req->create_response(restinio::status_ok()).done();
            }
            else return req->create_response(restinio::status_non_authoritative_information()).done();
        });
    }


    void get_favourite_posts(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get("/post/favourite/", [pool_ptr, logger_ptr](auto req, auto params) {
            logger_ptr->trace([]{return "called /post/favourite/";});
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }
            std::string username = auth::get_username(token, pool_ptr);
            if(username == "") {
                return req->create_response(restinio::status_unauthorized()).done();
            }

            logger_ptr->info( [username]{return fmt::format("favourite posts request from {}", username);}); // delete
            
            pqxx::result result = page::get_favourite_posts(username, pool_ptr);
            
            if(result.empty()) {
                return req->create_response(restinio::status_ok()).done();
            }
            return req->create_response()
                .append_header("Content-Type", "application/json; charset=utf-8")
                .set_body(cp::serialize(result))
                .done();
            
        });
    }

    void post_add_favourite_post(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_post("/post/favourite", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /post/favourite";});
            std::string endpoint = req->remote_endpoint().address().to_string();

            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());

            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if (token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            std::string username = auth::get_username(token, pool_ptr);

            if(username == "") {
                return req->create_response(restinio::status_unauthorized()).done();
            }

            logger_ptr->info( [username]{return fmt::format("add favourite post from {}", username);});

            if (new_body.HasMember("post_id")) {
                try {
                    page::add_favourite_post(new_body["post_id"].GetInt(), username, pool_ptr, logger_ptr);
                } catch (pqxx::unique_violation& e) {
                    logger_ptr->info( [endpoint]{return fmt::format("favourite post already exists from {}", endpoint);});
                    return req->create_response(restinio::status_forbidden()).done();
                } catch (pqxx::sql_error& e) {
                    logger_ptr->error( [endpoint, e]{return fmt::format("sql error {} from {}", e.what(), endpoint);});
                    return req->create_response(restinio::status_internal_server_error()).done();
                }
                logger_ptr->info( [endpoint]{return fmt::format("favourite post added from {}", endpoint);});
                return req->create_response().done();
            } else return req->create_response(restinio::status_non_authoritative_information()).done();
        });
    }

    void delete_favourite_post(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_delete(R"(/post/favourite/:id(\d+))", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /post/favourite/:id";});
            std::vector<std::string> url_parts = url::spilt_url_path(req->header().path(), "/");
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            int post_id = std::stoi(url::get_last_url_arg(req->header().path()));

            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if(!auth::is_authed(token, pool_ptr)) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            std::string username = auth::get_username(token, pool_ptr);

            page::delete_favourite_post(post_id, username, pool_ptr, logger_ptr);
            
            logger_ptr->info( [post_id]{return fmt::format("favourite post deleted with id {}", post_id);});
            return req->create_response()
                .append_header("Content-Type", "text/plain; charset=utf-8")
                .set_body("ok")
                .done();
            
        });
    }

    void rename_category(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_put("/post/rename_category", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /post/rename_category";});
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if (token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if (!auth::is_admin(token, pool_ptr)) {
                logger_ptr->info([]{return "not admin";});
                return req->create_response(restinio::status_unauthorized()).done();
            }
            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());

            if (new_body.HasMember("old_name") && new_body.HasMember("new_name")) {
                page::rename_category(new_body["old_name"].GetString(), new_body["new_name"].GetString(), pool_ptr, logger_ptr);
                return req->create_response(restinio::status_ok()).done();
            }
            else return req->create_response(restinio::status_non_authoritative_information()).done();
        });
    }

    void delete_post(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_delete("/post/delete", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /post/delete";});
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if (token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            // TODO: can delete if author
            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());
            if (new_body.HasParseError() || !new_body.IsObject()) {
                return req->create_response(restinio::status_bad_request()).done();
            }

            auto post_id = json_int_member(new_body, "post_id");
            if (!post_id.has_value() || *post_id <= 0) {
                return req->create_response(restinio::status_bad_request()).done();
            }

            try {
                pqxx::result post = page::get_page_content(*post_id, pool_ptr);
                if (post.empty()) {
                    return req->create_response(restinio::status_not_found()).done();
                }

                std::string post_author = row_string_or_empty(post[0], "author");
                std::string username = auth::get_username(token, pool_ptr);
                bool can_delete = post_author.empty()
                    ? auth::is_admin(token, pool_ptr)
                    : authors::check_if_avaluable_author(username, post_author, pool_ptr);
                if (!can_delete) {
                    logger_ptr->info([]{return "not admin";});
                    return req->create_response(restinio::status_unauthorized()).set_body("not your post - do not touch it!").done();
                }

                page::delete_post(*post_id, pool_ptr, logger_ptr);
                return req->create_response(restinio::status_ok())
                    .append_header("Content-Type", "text/plain; charset=utf-8")
                    .set_body("ok")
                    .done();
            } catch (const std::exception& e) {
                logger_ptr->error([post_id, e] {
                    return fmt::format("error deleting post {}: {}", *post_id, e.what());
                });
                return req->create_response(restinio::status_internal_server_error()).done();
            }
        });
    }   
    
    void update_post(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_put("/post/update", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /post/update";});
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if (token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if (!auth::is_admin(token, pool_ptr)) {
                logger_ptr->info([]{return "not admin";});
                return req->create_response(restinio::status_unauthorized()).done();
            }
            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());
            if (new_body.HasParseError() || !new_body.IsObject()) {
                return req->create_response(restinio::status_bad_request()).done();
            }

            auto post_id = json_int_member(new_body, "post_id");
            if (!post_id.has_value() || *post_id <= 0) {
                return req->create_response(restinio::status_bad_request()).done();
            }

            try {
                auto old_post = page::get_page_content(*post_id, pool_ptr);
                if (old_post.empty()) {
                    return req->create_response(restinio::status_not_found()).done();
                }

                logger_ptr->info([post_id] { return fmt::format("update post with id {}", *post_id); });

                std::string text, title, author;
                auto request_text = json_string_member(new_body, "text");
                if(new_body.HasMember("text")) {
                    if (!request_text.has_value()) {
                        return req->create_response(restinio::status_bad_request()).done();
                    }
                    text = *request_text;
                    assist::fix_new_lines(text);
                } else {
                    text = row_string_or_empty(old_post[0], "text");
                }
                auto request_title = json_string_member(new_body, "title");
                if(new_body.HasMember("title")) {
                    if (!request_title.has_value()) {
                        return req->create_response(restinio::status_bad_request()).done();
                    }
                    title = *request_title;
                    assist::fix_new_lines(title);
                } else {
                    title = row_string_or_empty(old_post[0], "title");
                }
                auto request_author = json_string_member(new_body, "author");
                if(new_body.HasMember("author")) {
                    if (json_string_member_has_invalid_type(new_body, "author")) {
                        return req->create_response(restinio::status_bad_request()).done();
                    }
                    if (request_author.has_value() && authors::check_if_avaluable_author(auth::get_username(token, pool_ptr), *request_author, pool_ptr)) {
                        author = *request_author;
                        assist::fix_new_lines(author);
                    } else {
                        author = row_string_or_empty(old_post[0], "author");
                    }
                } else {
                    author = row_string_or_empty(old_post[0], "author");
                }

                auto is_secret = new_body.HasMember("is_secret") ? json_bool_value(new_body["is_secret"]) : std::optional<bool>(row_bool_or_false(old_post[0], "is_secret"));
                auto likes = new_body.HasMember("likes") ? json_int_value(new_body["likes"]) : std::optional<int>(row_int_or_zero(old_post[0], "likes"));
                auto dislikes = new_body.HasMember("dislikes") ? json_int_value(new_body["dislikes"]) : std::optional<int>(row_int_or_zero(old_post[0], "dislikes"));
                auto saved_count = new_body.HasMember("saved_count") ? json_int_value(new_body["saved_count"]) : std::optional<int>(row_int_or_zero(old_post[0], "saved_count"));
                if (!is_secret.has_value() || !likes.has_value() || !dislikes.has_value() || !saved_count.has_value()) {
                    return req->create_response(restinio::status_bad_request()).done();
                }

                auto category_name = json_string_member(new_body, "category_name");
                auto post_path = json_string_member(new_body, "post_path");
                if (json_string_member_has_invalid_type(new_body, "category_name") ||
                    json_string_member_has_invalid_type(new_body, "post_path")) {
                    return req->create_response(restinio::status_bad_request()).done();
                }

                page::update_post(
                    *post_id,
                    *is_secret,
                    *likes,
                    *dislikes,
                    *saved_count,
                    title,
                    author,
                    text,
                    category_name.has_value() ? *category_name : row_string_or_empty(old_post[0], "category_name"),
                    post_path.has_value() ? *post_path : row_string_or_empty(old_post[0], "post_path"),
                    pool_ptr, logger_ptr
                );
                logger_ptr->info([post_id] { return fmt::format("post {} database row updated", *post_id); });

                std::vector<std::string> media_paths;
                page::med_to_vec(new_body, media_paths);
                if (!media_paths.empty()) {
                    page::add_media(*post_id, media_paths, pool_ptr, logger_ptr);
                    logger_ptr->info([post_id] { return fmt::format("post {} media updated", *post_id); });
                }

                pqxx::result updated_post = page::get_page_content(*post_id, pool_ptr);
                if (updated_post.empty()) {
                    logger_ptr->error([post_id] {
                        return fmt::format("post {} disappeared after update", *post_id);
                    });
                    return req->create_response(restinio::status_internal_server_error()).done();
                }

                pqxx::result response_post = updated_post;
                if (row_string_or_empty(updated_post[0], "post_path").empty()) {
                    std::string username = auth::get_username(token, pool_ptr);
                    response_post = social::get_post(
                        std::to_string(*post_id),
                        username,
                        security::can_read_secret_posts(username, pool_ptr),
                        pool_ptr);
                    if (response_post.empty()) {
                        logger_ptr->error([post_id] {
                            return fmt::format("post {} social response is empty after update", *post_id);
                        });
                        response_post = updated_post;
                    }
                }

                return req->create_response(restinio::status_ok())
                    .append_header("Content-Type", "application/json; charset=utf-8")
                    .set_body(cp::serialize_with_shift_day(response_post, pool_ptr))
                    .done();
            } catch (const std::exception& e) {
                logger_ptr->error([post_id, e] {
                    return fmt::format("error updating post {}: {}", *post_id, e.what());
                });
                return req->create_response(restinio::status_internal_server_error()).done();
            }
        });
    }

    void add_media(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_post("/post/add_media", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /post/add_media";});
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if (token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if (!auth::is_admin(token, pool_ptr)) {
                logger_ptr->info([]{return "not admin";});
                return req->create_response(restinio::status_unauthorized()).done();
            }
            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());

            if (new_body.HasMember("post_id") && new_body.HasMember("media")) {
                std::vector<std::string> media_paths;
                page::med_to_vec(new_body, media_paths);
                if (!media_paths.empty()) {
                    page::add_media(new_body["post_id"].GetInt(), media_paths, pool_ptr, logger_ptr);
                }
                return req->create_response(restinio::status_ok())
                    .append_header("Content-Type", "text/plain; charset=utf-8")
                    .set_body("ok")
                    .done();
            } else return req->create_response(restinio::status_non_authoritative_information()).done();
        });
    }

    void delete_media(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_delete("/post/delete_media", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /post/delete_media";});
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if (token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if (!auth::is_admin(token, pool_ptr)) {
                logger_ptr->info([]{return "not admin";});
                return req->create_response(restinio::status_unauthorized()).done();
            }
            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());

            if (new_body.HasMember("post_id") && new_body.HasMember("media")) {
                std::vector<std::string> media_paths;
                page::med_to_vec(new_body, media_paths);
                if (!media_paths.empty()) {
                    page::delete_media(new_body["post_id"].GetInt(), media_paths, pool_ptr, logger_ptr);
                }
                return req->create_response(restinio::status_ok())
                    .append_header("Content-Type", "text/plain; charset=utf-8")
                    .set_body("ok")
                    .done();
            } else return req->create_response(restinio::status_non_authoritative_information()).done();
        });
    }

    void reveal_secret_page(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/post/reveal_secret_link/:id(\d+))", [pool_ptr, logger_ptr](auto req, auto params) {
            logger_ptr->trace([]{return "called /post/reveal_secret_link/:id";});
            int post_id = url::last_int_from_url_path(req->header().path());
            if (post_id <= 0) {
                return req->create_response(restinio::status_bad_request()).done();
            }
            const std::string token = security::bearer_or_cookie_token(req->header());
            const std::string actor = auth::get_username(token, pool_ptr);
            if (actor.empty() || !auth::is_admin(token, pool_ptr)) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            try {
                const std::string reveal_token = security::create_post_reveal_token(post_id, actor, pool_ptr);
                return req->create_response(restinio::status_ok())
                    .append_header("Content-Type", "application/json; charset=utf-8")
                    .set_body(fmt::format(R"({{"post_id":{},"reveal_url":"{}"}})", post_id, page::reveal_secret_url(reveal_token)))
                    .done();
            } catch (const std::exception& e) {
                logger_ptr->error([e] { return fmt::format("Error: {}", e.what()); });
                return req->create_response(restinio::status_internal_server_error()).done();
            }
        });

        router.get()->http_get(R"(/post/reveal_secret/:token([a-fA-F0-9]+))", [pool_ptr, logger_ptr](auto req, auto params) {
            logger_ptr->trace([]{return "called /post/reveal_secret/:token";});
            std::string token = url::get_last_url_arg(req->header().path());
            if (token.size() != 64) {
                return req->create_response(restinio::status_bad_request()).done();
            }
            std::optional<int> post_id = security::post_id_from_reveal_token(token, pool_ptr);
            if (!post_id.has_value()) {
                return req->create_response(restinio::status_forbidden()).done();
            }
            page::reveal_secret_page(*post_id, pool_ptr);
            return req->create_response(restinio::status_ok())
                .append_header("Content-Type", "text/plain; charset=utf-8")
                .set_body(Comment::giveMe().reveal_secret)
                .done();
        });
    }
}
