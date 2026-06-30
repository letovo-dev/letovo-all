#pragma once

#include "../basic/pqxx_cp.h"
#include <pqxx/pqxx>
#include <restinio/all.hpp>
#include "../basic/url_parser.h"
#include "../basic/auth.h"


namespace actives {
    struct active_obj {
        int activeId = 0;
        std::string activeTicker = "";
    };

    pqxx::result all_public(std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    pqxx::result active(std::shared_ptr<cp::ConnectionsManager> pool_ptr, actives::active_obj act);

    pqxx::result active_history(std::shared_ptr<cp::ConnectionsManager> pool_ptr,  actives::active_obj act);

    pqxx::result user_actives(std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::string user_name);

    pqxx::result user_actives(std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::string user_name, actives::active_obj act);

    pqxx::result user_history(std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::string user_name);

    void add_active(std::shared_ptr<cp::ConnectionsManager> pool_ptr, const std::string user_name, active_obj act);
}

namespace actives::server {
    void all_public(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void active(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void history(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void user_actives(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void user_history(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
}
