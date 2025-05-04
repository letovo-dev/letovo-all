#pragma once
#include <restinio/all.hpp>
#include "pqxx_cp.h"
#include <pqxx/pqxx>
#include <unordered_set>
#include "rapidjson/document.h"
#include "asio/ip/detail/endpoint.hpp"
#include "hash.h"
#include "auth.h"
#include <vector>

namespace achivements {
    pqxx::result full_user_achivements(std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    pqxx::result user_achivements(std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    bool add_achivement(std::string username, int achivement_id, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    bool delete_achivement(std::string username, int achivement_id, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    pqxx::result achivements_tree(int tree_id, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    pqxx::result achivement_info(int achivement_id, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    int create_achivement(std::string name, int tree_id, int level, std::string pic, std::string description, int stages, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    pqxx::result get_achivement_categories(std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    pqxx::result get_achivement_by_category(std::string category, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    std::vector<std::string> achivement_pictures();
} // namespace achivements

namespace achivements::server {
    void user_achivemets(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void full_user_achivemets(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void add_achivement(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void delete_achivement(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void achivements_tree(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void achivement_info(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void create_achivement(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void achivement_pictures(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void get_achivement_categories(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void get_achivement_by_category(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
} // namespace achivements::server
