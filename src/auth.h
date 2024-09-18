#pragma once

#include <jwt-cpp/jwt.h>
#include <httplib.h>
#include "pqxx_cp.h"
#include <pqxx/pqxx>
#include <unordered_set> 
#include "spdlog/spdlog.h"

void enable_auth_reg(std::shared_ptr<httplib::Server> svr_ptr, std::shared_ptr<cp::connection_pool> pool_ptr);

bool is_authed(std::string token, std::shared_ptr<cp::connection_pool> pool_ptr);

bool is_authed(const httplib::Request& req, std::shared_ptr<cp::connection_pool> pool_ptr);

void test_http(cp::connection_pool& pool_ptr);