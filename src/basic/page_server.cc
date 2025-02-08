#include "page_server.h"

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
        pqxx::result result = con->execute_params("SELECT \"username\", \"avatar_pic\" from \"user\" WHERE \"username\"=($1);", params);
        pool_ptr->returnConnection(std::move(con));
        return result;
    }

    int add_page_by_content(bool is_secret, bool is_published, int likes, std::string title, std::string author, std::string text, std::string post_path, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {post_path, std::to_string(is_secret), std::to_string(is_published), std::to_string(likes), title, author, text};
        pqxx::result result = con->execute_params("INSERT INTO \"posts\" (\"post_path\", \"is_secret\", \"is_published\", \"likes\", \"title\", \"author\", \"text\") VALUES ($1, $2, $3, $4, $5, $6, $7) RETURNING \"post_id\";", params);
        pool_ptr->returnConnection(std::move(con));
        return result[0]["post_id"].as<int>();
    }

    int add_page_by_page(std::string post_path, std::string text, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {post_path};
        pqxx::result result = con->execute_params("INSERT INTO \"posts\" (\"post_path\") VALUES ($1) RETURNING \"post_id\";", params);
        pool_ptr->returnConnection(std::move(con));
        assist::create_file(post_path, text, logger_ptr);
        return result[0]["post_id"].as<int>();
    }

    void update_likes(int post_id, int likes, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {std::to_string(likes), std::to_string(post_id)};
        con->execute_params("UPDATE \"posts\" SET \"likes\"=($1) WHERE \"post_id\"=($2);", params, true);
        pool_ptr->returnConnection(std::move(con));
    }

    void delete_page(int post_id, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {std::to_string(post_id)};
        con->execute_params("DELETE FROM \"posts\" WHERE \"post_id\"=($1);", params, true);
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
}


namespace page::server {
    void get_page_content(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/post/:id(\d+))", [pool_ptr, logger_ptr](auto req, auto params) {
            std::string endpoint = req->remote_endpoint().address().to_string();
            auto qrl = req->header().path();

            int postid = url::last_int_from_url_path(qrl);

            if (postid <= 0) {
                return req->create_response(restinio::status_bad_request()).done();
            }

            pqxx::result result = page::get_page_content(postid, pool_ptr);
            if (result[0]["is_secret"].as<bool>() == true || result[0]["is_published"].as<bool>() == false) {
                logger_ptr->info( [endpoint]{return fmt::format("page request from {} is secret", endpoint);});
                return req->create_response(restinio::status_not_found()).done();
            }

            if(result.empty()) {
                return req->create_response(restinio::status_bad_gateway()).done();
            }

            return req->create_response().set_body(cp::serialize(result)).done();
            
        });
    }

    void get_page_author(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/post/author/:username([a-zA-Z0-9\-]+))", [pool_ptr, logger_ptr](auto req, auto params) {
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
            return req->create_response().set_body(cp::serialize(result)).done();
            
        });
    }

    void add_page_by_content(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_post("/post/add_page_content", [pool_ptr, logger_ptr](auto req, auto) {
            std::string endpoint = req->remote_endpoint().address().to_string();

            if(!auth::is_authed_by_body(req->body(), pool_ptr)) {
                return req->create_response(restinio::status_unauthorized()).done();
            }

            logger_ptr->info( [endpoint]{return fmt::format("page request from {}", endpoint);});

            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());

            std::unordered_map<std::string, std::any> new_map = assist::parse_page_content(new_body);

            if (!new_map.empty()) {
                std::string path = std::any_cast<std::string>(new_map["post_path"]); 
                logger_ptr->info( [endpoint, path]{return fmt::format("path from {} is {}", endpoint, path);});
                if(path[0] == '/' || path.find("..") != std::string::npos) {
                    logger_ptr->info( [endpoint]{return fmt::format("bad path from {}", endpoint);});
                    return req->create_response(restinio::status_forbidden()).done();
                }
                int post_id;
                try {
                    post_id = page::add_page_by_content(
                        std::any_cast<bool>(new_map["is_secret"]),
                        std::any_cast<bool>(new_map["is_published"]),
                        std::any_cast<int>(new_map["likes"]),
                        std::any_cast<std::string>(new_map["title"]),
                        std::any_cast<std::string>(new_map["author"]),
                        std::any_cast<std::string>(new_map["text"]),
                        std::any_cast<std::string>(new_map["post_path"]),
                        pool_ptr, logger_ptr);
                } catch (pqxx::sql_error& e) {
                    logger_ptr->error( [endpoint]{return fmt::format("sql error from {}", endpoint);});
                    return req->create_response(restinio::status_non_authoritative_information()).done();
                } catch (std::bad_any_cast& e) {
                    logger_ptr->error( [endpoint]{return fmt::format("bad request from {}", endpoint);});
                    return req->create_response(restinio::status_non_authoritative_information()).done();
                }
                
                logger_ptr->info( [post_id]{return fmt::format("page added with id {}", post_id);});
                if (new_map["author"].has_value()) 
                    assist::create_mdx_from_template_file(path, std::any_cast<std::string>(new_map["title"]), std::any_cast<std::string>(new_map["author"]), std::to_string(post_id), logger_ptr);
                else 
                    assist::create_mdx_from_template_file(path, std::any_cast<std::string>(new_map["title"]), std::to_string(post_id), logger_ptr);
                return req->create_response().set_body(cp::serialize(page::get_page_content(post_id, pool_ptr))).done();
            }
            else {
                logger_ptr->info( [endpoint]{return fmt::format("bad request from {}, not enough args", endpoint);});
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }
        });
    }

    void add_page_by_page(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_post("/post/add_page", [pool_ptr, logger_ptr](auto req, auto) {
            if(!auth::is_authed_by_body(req->body(), pool_ptr)) {
                return req->create_response(restinio::status_unauthorized()).done();
            }

            std::string endpoint = req->remote_endpoint().address().to_string();

            logger_ptr->info( [endpoint]{return fmt::format("page request from {}", endpoint);});

            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());

            if (new_body.HasMember("post_path")) {
                std::string path = new_body["post_path"].GetString();
                if(path[0] == '/' || path.find("..") != std::string::npos) {
                    logger_ptr->info( [endpoint]{return fmt::format("bad path from {}", endpoint);});
                    return req->create_response(restinio::status_forbidden()).done();
                }

                int post_id = page::add_page_by_page(path, new_body["text"].GetString(), pool_ptr, logger_ptr);
                logger_ptr->info( [endpoint, path]{return fmt::format("page added from {} to {}", endpoint, path);});
                assist::create_file(path, new_body["text"].GetString(), logger_ptr);
                return req->create_response().set_body(cp::serialize(page::get_page_content(post_id, pool_ptr))).done();

            }
            else return req->create_response(restinio::status_non_authoritative_information()).done();
        });
    }

    void update_likes(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_put("/post/update_likes", [pool_ptr, logger_ptr](auto req, auto) {
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

    void enable_delete(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_delete(R"(/post/delete/:id(\d+))", [pool_ptr, logger_ptr](auto req, auto) {
            std::string token;
            try {
                token = req -> header().get_field("token");
            } catch (const std::exception& e) {
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }
            int post_id = std::stoi(url::get_last_url_arg(req->header().path()));

            if(token.empty()) {
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }
            if(!auth::is_authed(token, pool_ptr)) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            page::delete_page(post_id,pool_ptr, logger_ptr);
            
            logger_ptr->info( [post_id]{return fmt::format("page deleted with id {}", post_id);});
            return req->create_response().set_body("ok").done();
            
        });
    }


    void get_favourite_posts(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get("/post/favourite/", [pool_ptr, logger_ptr](auto req, auto params) {
            std::string token;
            try {
                token = req -> header().get_field("token");
            } catch (const std::exception& e) {
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
            return req->create_response().set_body(cp::serialize(result)).done();
            
        });
    }

    void post_add_favourite_post(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_post("/post/favourite", [pool_ptr, logger_ptr](auto req, auto) {
            std::string endpoint = req->remote_endpoint().address().to_string();

            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());

            std::string token;
            try {
                token = req -> header().get_field("token");
            } catch (const std::exception& e) {
                return req->create_response(restinio::status_non_authoritative_information()).done();
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
            }
            else return req->create_response(restinio::status_non_authoritative_information()).done();
        });
    }

    void delete_favourite_post(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_delete(R"(/post/favourite/:id(\d+))", [pool_ptr, logger_ptr](auto req, auto) {
            std::vector<std::string> url_parts = url::spilt_url_path(req->header().path(), "/");

            std::string token = url_parts[3];
            int post_id = std::stoi(url::get_last_url_arg(req->header().path()));

            if(token.empty()) {
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }
            if(!auth::is_authed(token, pool_ptr)) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            std::string username = auth::get_username(token, pool_ptr);

            page::delete_favourite_post(post_id, username, pool_ptr, logger_ptr);
            
            logger_ptr->info( [post_id]{return fmt::format("favourite post deleted with id {}", post_id);});
            return req->create_response().set_body("ok").done();
            
        });
    }
}