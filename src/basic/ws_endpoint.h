#pragma once

#include <memory>
#include <restinio/all.hpp>

#include "pqxx_cp.h"
#include "ws_event_bus.h"
#include "ws_topic_authorizer.h"

namespace ws::server {

void register_endpoint(
    std::unique_ptr<restinio::router::express_router_t<>>& router,
    std::shared_ptr<cp::ConnectionsManager> pool_ptr,
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr,
    std::shared_ptr<EventBus> bus_ptr,
    std::shared_ptr<TopicAuthorizer> authorizer_ptr);

void list_active_sessions(
    std::unique_ptr<restinio::router::express_router_t<>>& router,
    std::shared_ptr<cp::ConnectionsManager> pool_ptr,
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr,
    std::shared_ptr<EventBus> bus_ptr);

} // namespace ws::server
