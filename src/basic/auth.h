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

namespace auth { 
    std::string get_username(std::string token, std::shared_ptr<cp::connection_pool> pool_ptr);
    
    bool is_authed(std::string token, std::shared_ptr<cp::connection_pool> pool_ptr);
    bool is_admin(std::string token, std::shared_ptr<cp::connection_pool> pool_ptr);
    bool is_user(std::string token, std::shared_ptr<cp::connection_pool> pool_ptr);
    bool is_admin_by_uname(std::string username, std::shared_ptr<cp::connection_pool> pool_ptr);
    bool is_authed_by_body(std::string req_body, std::shared_ptr<cp::connection_pool> pool_ptr);
}
namespace auth::server {
    void enable_reg(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
    void enable_auth(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
    void enable_delete(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
    void am_i_authed(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
    void is_user(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
    void am_i_admin(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
    void add_userrights(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
    void change_username(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
    void change_password(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
    void is_user_active(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
    void add_new_user(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
}

