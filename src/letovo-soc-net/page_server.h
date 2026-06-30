#pragma once
#include <restinio/all.hpp>
#include <pqxx/pqxx>
#include <vector>
#include <fmt/format.h>
#include <rapidjson/document.h>
#include <unordered_map>
#include <filesystem>
#include <any>
#include "authors.h"
#include "social.h"
#include "../basic/pqxx_cp.h"
#include "../basic/assist_funcs.h"
#include "../basic/url_parser.h"
#include "../basic/auth.h"
#include "../basic/config.h"
#include "../basic/comment.h"
#include "../basic/config.h"
namespace page {

    pqxx::result get_page_content(int post_id, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    pqxx::result get_page_author(std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    int add_page_by_content(bool is_secret, int likes, int dislikes, int saved, std::string title, std::string author, std::string text, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    int add_page_by_page(std::string post_path, std::string category, std::string title, bool is_secret, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void update_likes(int post_id, int likes, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    pqxx::result get_favourite_posts(std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    pqxx::result add_favourite_post(int post_id, std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void delete_favourite_post(int post_id, std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void rename_category(std::string old_name, std::string new_name, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void delete_post(int post_id, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void update_post(int post_id, bool is_secret, int likes, int dislikes, int saved, std::string title, std::string author, std::string text, std::string category, std::string post_path, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void add_media(int post_id, std::vector<std::string> &media_paths, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void delete_media(int post_id, std::vector<std::string> &media_paths, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void med_to_vec(const rapidjson::Document& new_body, std::vector<std::string>& out_media);

    void reveal_secret_page(int post_id, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    std::string reveal_secret_url(const std::string& token);
}

namespace page::server {
    void get_page_content(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void get_page_author(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void add_page(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void update_likes(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void get_favourite_posts(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void post_add_favourite_post(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void delete_favourite_post(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void delete_post(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
    
    void rename_category(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void update_post(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void add_media(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void delete_media(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
    
    void reveal_secret_page(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
}
