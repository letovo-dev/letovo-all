#pragma once
#include <jwt-cpp/jwt.h>
#include <restinio/all.hpp>
#include "pqxx_cp.h"
#include <pqxx/pqxx>
#include <unordered_set>
// #include "spdlog/spdlog.h"
#include "rapidjson/document.h"
#include "asio/ip/detail/endpoint.hpp"
#include "hash.h"
#include "url_parser.h"
#include <vector>
#include "user_data.h"

namespace auth {
    std::string get_username(std::string token, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    bool is_authed(std::string token, std::shared_ptr<cp::ConnectionsManager> pool_ptr);
    bool is_admin(std::string token, std::shared_ptr<cp::ConnectionsManager> pool_ptr);
    bool is_user(std::string token, std::shared_ptr<cp::ConnectionsManager> pool_ptr);
    bool is_rights_by_username(std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::string rights="admin");
    bool is_authed_by_body(std::string req_body, std::shared_ptr<cp::ConnectionsManager> pool_ptr);
    bool auth(std::string token, std::string password, std::shared_ptr<cp::ConnectionsManager> pool_ptr);
    bool reg(std::string username, std::string password_hash, std::string userid, std::shared_ptr<cp::ConnectionsManager> pool_ptr);
    bool delete_user(std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr);
    bool add_userrights(std::string username, std::string rights, std::shared_ptr<cp::ConnectionsManager> pool_ptr);
    bool change_username(std::string username, std::string new_username, std::shared_ptr<cp::ConnectionsManager> pool_ptr);
    bool change_password(std::string username, std::string new_password, std::shared_ptr<cp::ConnectionsManager> pool_ptr);
    bool register_true(std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr);
} // namespace auth
namespace auth::server {
    void enable_reg(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
    void enable_auth(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
    void enable_delete(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
    void am_i_authed(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
    void is_user(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
    void am_i_admin(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
    void add_userrights(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
    void change_username(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
    void change_password(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
    void is_user_active(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
    void add_new_user(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
    void register_true(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
    void is_admin(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
} // namespace auth::server
