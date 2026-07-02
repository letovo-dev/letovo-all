#pragma once

#include "pqxx_cp.h"

#include <memory>
#include <restinio/all.hpp>
#include <string>

namespace analytics {

std::string normalize_route(const std::string &route);

void record_event(const std::string &username, const std::string &session_id,
                  const std::string &ip_address,
                  const std::string &user_agent, const std::string &method,
                  const std::string &route, int status, int duration_ms,
                  const std::string &client_event,
                  const std::string &metadata_json,
                  std::shared_ptr<cp::ConnectionsManager> pool_ptr,
                  std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

std::string summary_json(std::shared_ptr<cp::ConnectionsManager> pool_ptr);
std::string daily_json(int days,
                       std::shared_ptr<cp::ConnectionsManager> pool_ptr);
std::string active_users_json(
    int window_minutes, std::shared_ptr<cp::ConnectionsManager> pool_ptr);
std::string user_activity_json(
    const std::string &username, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

} // namespace analytics

namespace analytics::server {

void register_routes(
    std::unique_ptr<restinio::router::express_router_t<>> &router,
    std::shared_ptr<cp::ConnectionsManager> pool_ptr,
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

} // namespace analytics::server
