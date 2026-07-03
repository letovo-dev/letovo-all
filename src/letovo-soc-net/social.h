#pragma once
#include <restinio/all.hpp>
#include <pqxx/pqxx>
#include <vector>
#include <optional>
#include <fmt/format.h>
#include <rapidjson/document.h>
#include ".././basic/pqxx_cp.h"
#include <unordered_map>
#include <filesystem>
#include <any>
#include "authors.h"
#include "../basic/assist_funcs.h"
#include "../basic/url_parser.h"
#include "../basic/auth.h"
#include "../basic/media.h"
#include "../basic/security.h"
#include "../basic/config.h"
#include "../basic/assist_funcs.h"

namespace social {
    pqxx::result get_authors_list(std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    pqxx::result get_news(std::string start, int size, std::string username, std::optional<std::string> date, bool include_secret, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    pqxx::result get_all_posts(std::string username, bool include_secret, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    pqxx::result get_comments(std::string post_id, std::string start, int size, std::string username, bool include_secret, std::shared_ptr<cp::ConnectionsManager> pool_ptr);
        
    pqxx::result get_post_media(std::string post_id, bool pics, bool include_secret, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    std::string get_news_related(std::vector<int> post_ids, int comments_size, std::string username, bool include_secret, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    pqxx::result get_all_titles(bool include_secret, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    pqxx::result get_post(std::string post_id, std::string usenrame, bool include_secret, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    pqxx::result get_posts_by_author(std::string author, std::string username, bool include_secret, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    pqxx::result get_saved_posts(std::string username, bool include_secret, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    void add_like(int like, std::string post_id, std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    void delete_like(int like, std::string post_id, std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    int add_comment(std::string comment, std::string post_id, std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    void save_post(std::string post_id, std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    void delete_saved_post(std::string post_id, std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    pqxx::result get_post_categories(std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    pqxx::result get_post_by_category(std::string category, bool include_secret, std::shared_ptr<cp::ConnectionsManager> pool_ptr);
}

namespace social::server {
    void get_authors_list(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void get_news(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void get_news_related(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void get_comments(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void get_post_media(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void get_post_pics(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void get_post(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void get_all_posts(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
    
    void get_posts_by_author(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void get_all_titles(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void get_saved_posts(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void search_by_title(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void add_like(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void add_dislike(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void delete_like(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void delete_dislike(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void add_comment(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void save_post(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void delete_saved_post(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void get_post_categories(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void get_post_by_category(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
}
