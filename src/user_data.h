#pragma once
#include <restinio/all.hpp>
#include "pqxx_cp.h"
#include <pqxx/pqxx>
#include <unordered_set> 
// #include "spdlog/spdlog.h"
#include "rapidjson/document.h" 
#include "asio/ip/detail/endpoint.hpp"
#include "hash.h"
#include "auth.h"
#include <vector>   

namespace user {
    pqxx::row role(int role_id, std::shared_ptr<cp::connection_pool> pool_ptr);

    int role_id(std::string role, std::string department, std::shared_ptr<cp::connection_pool> pool_ptr);

    pqxx::row user_info(std::string username, std::shared_ptr<cp::connection_pool> pool_ptr);

    pqxx::row user_role(std::string username, std::shared_ptr<cp::connection_pool> pool_ptr);

    pqxx::result user_roles(std::string username, std::shared_ptr<cp::connection_pool> pool_ptr);

    bool add_user_role(std::string username, std::string role, std::string department, std::shared_ptr<cp::connection_pool> pool_ptr);

    bool add_user_role(std::string username, int role_id, std::shared_ptr<cp::connection_pool> pool_ptr);

    // do we even need this?
    bool delete_user_role(std::string username, std::string role, std::string department, std::shared_ptr<cp::connection_pool> pool_ptr);

    int create_role(std::string role, std::string department, int rang,std::shared_ptr<cp::connection_pool> pool_ptr);
}


namespace user::server {
    void user_info(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void user_roles(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void add_user_role(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void delete_user_role(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void create_role(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
}