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

void enable_auth_reg(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::connection_pool> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

bool is_authed(std::string token, std::shared_ptr<cp::connection_pool> pool_ptr);

bool is_authed_by_body(std::string req_body, std::shared_ptr<cp::connection_pool> pool_ptr);