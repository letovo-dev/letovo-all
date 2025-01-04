#include <restinio/all.hpp>
#include <restinio/tls.hpp>
#include "rapidjson/document.h"
#include <iostream>

#include "./letovo-wiki/page_server.h"

#include "./market/transactions.h"

// do i need this?
#include <fmt/format.h>
#include <fmt/ostream.h>

// server functions
#include "./basic/auth.h"
#include "./basic/config.h"
#include "./basic/user_data.h"

#include <filesystem>
namespace fs = std::filesystem;

using namespace restinio;

namespace rr = restinio::router;
using router_t = rr::express_router_t<>;

void hi(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
    router.get()->http_get(
        "/hi",
        [logger_ptr](auto req, auto) {

            asio_ns::ip::tcp::endpoint endpoint = req->remote_endpoint();

            logger_ptr->info( []{return fmt::format("hi recieved");});
            
            std::cout<<"endpoint: "<<endpoint.address().to_string()<<std::endl;
            return req->create_response().set_body(endpoint.address().to_string()).done();
    }); 
}

std::unique_ptr<restinio::router::express_router_t<>> create(std::shared_ptr<cp::connection_pool> pool_ptr) {
    auto router = std::make_unique<router::express_router_t<>>();

    auto logger_ptr = std::make_shared<restinio::shared_ostream_logger_t>();
    
    hi(router, logger_ptr);

    page::server::get_page_content(router, pool_ptr, logger_ptr);
    page::server::get_page_author(router, pool_ptr, logger_ptr);
    page::server::add_page_by_content(router, pool_ptr, logger_ptr);
    page::server::add_page_by_page(router, pool_ptr, logger_ptr);
    page::server::update_likes(router, pool_ptr, logger_ptr);
    page::server::enable_delete(router, pool_ptr, logger_ptr);

    page::server::get_favourite_posts(router, pool_ptr, logger_ptr);
    page::server::post_add_favourite_post(router, pool_ptr, logger_ptr);
    page::server::delete_favourite_post(router, pool_ptr, logger_ptr);

    auth::server::enable_reg(router, pool_ptr, logger_ptr);
    auth::server::enable_auth(router, pool_ptr, logger_ptr);
    auth::server::add_userrights(router, pool_ptr, logger_ptr);
    auth::server::am_i_authed(router, pool_ptr, logger_ptr);
    auth::server::am_i_admin(router, pool_ptr, logger_ptr);
    auth::server::enable_delete(router, pool_ptr, logger_ptr);
    auth::server::is_user_active(router, pool_ptr, logger_ptr);

    user::server::user_info(router, pool_ptr, logger_ptr);
    user::server::user_roles(router, pool_ptr, logger_ptr);
    user::server::add_user_role(router, pool_ptr, logger_ptr);
    user::server::delete_user_role(router, pool_ptr, logger_ptr);
    user::server::create_role(router, pool_ptr, logger_ptr);
    user::server::department_roles(router, pool_ptr, logger_ptr);
    user::server::department_name(router, pool_ptr, logger_ptr);

    transactions::server::transfer(router, pool_ptr, logger_ptr);
    transactions::server::get_balance(router, pool_ptr, logger_ptr);
    transactions::server::get_transactions(router, pool_ptr, logger_ptr);

    return router;
}


int main()
{
    using namespace std::chrono;

    std::shared_ptr<cp::connection_pool> pool_ptr = std::make_shared<cp::connection_pool>(Config::giveMe().sql_config);

    auto router = create(pool_ptr);

    std::string certs_dir = Config::giveMe().server_config.certs_path;

    using traits_t =
        restinio::single_thread_tls_traits_t<
            restinio::asio_timer_manager_t,
            restinio::single_threaded_ostream_logger_t,
            restinio::router::express_router_t<> >;

    namespace asio_ns = restinio::asio_ns;

    asio_ns::ssl::context tls_context{ asio_ns::ssl::context::sslv23 };
    tls_context.set_options(
        asio_ns::ssl::context::default_workarounds
        | asio_ns::ssl::context::no_sslv2
        | asio_ns::ssl::context::single_dh_use );

    tls_context.use_certificate_chain_file( certs_dir + "/server.pem" );
    tls_context.use_private_key_file(
        certs_dir + "/key.pem",
        asio_ns::ssl::context::pem );
    tls_context.use_tmp_dh_file( certs_dir + "/dh2048.pem" );


    using traits_t =
        restinio::single_thread_tls_traits_t<
            restinio::asio_timer_manager_t,
            restinio::single_threaded_ostream_logger_t,
            router_t >;
    
    restinio::run(
			restinio::on_thread_pool<traits_t>(Config::giveMe().server_config.thread_pool_size)
				.address( Config::giveMe().server_config.adress )
                .port( Config::giveMe().server_config.port )
				.request_handler( move(router))
                .tls_context( std::move(tls_context) )
    );
    return 0;
}