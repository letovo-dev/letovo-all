#pragma once

#include <restinio/all.hpp>

namespace ws {

struct server_traits : public restinio::default_traits_t {
    using request_handler_t = restinio::router::express_router_t<>;
};

} // namespace ws
