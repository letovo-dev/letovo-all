#pragma once
#include <restinio/all.hpp>
#include "pqxx_cp.h"
#include <pqxx/pqxx>
#include <unordered_set>
#include "media.h"
#include "rapidjson/document.h"
#include "asio/ip/detail/endpoint.hpp"
#include "hash.h"
#include "auth.h"
#include <vector>

namespace user {
    pqxx::result role(int role_id, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    int role_id(std::string role, int department, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    pqxx::result user_info(std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    pqxx::result user_role(std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    pqxx::result user_roles(std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    bool add_user_role(std::string username, std::string role, int department, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    bool add_user_role(std::string username, int role_id, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    // do we even need this?
    bool delete_user_role(std::string username, std::string role, int department, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    int create_role(std::string role, int department, int rang, int payment, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    pqxx::result department_roles(int department, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    std::string department_name(int department, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    int department_id(std::string department, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    int best_users_role_by_department(std::string username, int department, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    int set_users_department(std::string username, int department, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    int set_users_department(std::string username, std::string department, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    int starter_role(int department, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    int starter_role(std::string department, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    pqxx::result all_departments(std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    void set_avatar(std::string username, std::string avatar, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    std::vector<std::string> all_avatars();

} // namespace user

namespace user::server {
    void user_info(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void user_roles(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void add_user_role(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void delete_user_role(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void create_role(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void department_roles(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void department_name(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void set_users_department(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void all_departments(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void starter_role(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void all_avatars(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void set_avatar(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
} // namespace user::server
