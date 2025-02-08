#include <restinio/all.hpp>
#include <restinio/tls.hpp>
#include "rapidjson/document.h"
#include <iostream>

// do i need this?
#include <fmt/format.h>
#include <fmt/ostream.h>

#include "./basic/checks.h"

// server functions
#include "./basic/auth.h"
#include "./basic/config.h"
#include "./basic/user_data.h"
#include "./basic/media.h"
#include "./basic/achivements.h"
#include "./market/transactions.h"
#include "./basic/page_server.h"

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

std::unique_ptr<restinio::router::express_router_t<>> create(std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
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
    auth::server::is_user(router, pool_ptr, logger_ptr);

    user::server::user_info(router, pool_ptr, logger_ptr);
    user::server::user_roles(router, pool_ptr, logger_ptr);
    user::server::add_user_role(router, pool_ptr, logger_ptr);
    user::server::delete_user_role(router, pool_ptr, logger_ptr);
    user::server::create_role(router, pool_ptr, logger_ptr);
    user::server::department_roles(router, pool_ptr, logger_ptr);
    user::server::department_name(router, pool_ptr, logger_ptr);
    user::server::set_users_department(router, pool_ptr, logger_ptr);
    user::server::all_departments(router, pool_ptr, logger_ptr);
    user::server::starter_role(router, pool_ptr, logger_ptr);

    transactions::server::transfer(router, pool_ptr, logger_ptr);
    transactions::server::get_balance(router, pool_ptr, logger_ptr);
    transactions::server::get_transactions(router, pool_ptr, logger_ptr);

    achivements::server::user_achivemets(router, pool_ptr, logger_ptr);
    achivements::server::add_achivement(router, pool_ptr, logger_ptr);
    achivements::server::delete_achivement(router, pool_ptr, logger_ptr);
    achivements::server::achivements_tree(router, pool_ptr, logger_ptr);
    achivements::server::achivement_info(router, pool_ptr, logger_ptr);
    achivements::server::create_achivement(router, pool_ptr, logger_ptr);

    media::server::get_file(router, pool_ptr, logger_ptr);
    
    return router;
}


int main()
{
    using namespace std::chrono;

    std::shared_ptr<cp::ConnectionsManager> pool_ptr = std::make_shared<cp::ConnectionsManager>(Config::giveMe().sql_config);

    pool_ptr -> connect();

    pre_run_checks::do_checks(pool_ptr);

    auto router = create(pool_ptr);

    using traits_t =
        restinio::single_thread_tls_traits_t<
            restinio::asio_timer_manager_t,
            restinio::single_threaded_ostream_logger_t,
            restinio::router::express_router_t<> >;

    struct traits: public default_traits_t
    {
        using request_handler_t = restinio::router::express_router_t<>;
    };
    
    restinio::run(
			restinio::on_thread_pool<traits>(Config::giveMe().server_config.thread_pool_size)
				.address( Config::giveMe().server_config.adress )
                .port( Config::giveMe().server_config.port )
				.request_handler( move(router))
                // .tls_context( std::move(tls_context) )
    );
    return 0;
}