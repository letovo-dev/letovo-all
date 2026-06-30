#pragma once

#include <restinio/all.hpp>

#include "../basic/auth.h"
#include "../basic/pqxx_cp.h"

namespace authors {
    pqxx::result get_avaluable_authors(std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    bool check_if_avaluable_author(std::string username, std::string author, std::shared_ptr<cp::ConnectionsManager> pool_ptr);
}

namespace authors::server {
    void get_avaluable_authors(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
}