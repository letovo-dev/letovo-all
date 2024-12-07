#include <restinio/all.hpp>
#include "rapidjson/document.h"
#include <iostream>
#include "./letovo-wiki/page_server.h"

// server functions
#include "auth.h"
#include "config.h"

using namespace restinio;


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

    page::get_page_content(router, pool_ptr, logger_ptr);
    page::get_page_author(router, pool_ptr, logger_ptr);
    page::add_page_by_content(router, pool_ptr, logger_ptr);
    page::add_page_by_page(router, pool_ptr, logger_ptr);
    page::update_likes(router, pool_ptr, logger_ptr);
    page::enable_delete(router, pool_ptr, logger_ptr);

    page::get_favourite_posts(router, pool_ptr, logger_ptr);
    page::post_add_favourite_post(router, pool_ptr, logger_ptr);
    page::delete_favourite_post(router, pool_ptr, logger_ptr);

    auth::enable_reg(router, pool_ptr, logger_ptr);
    auth::enable_auth(router, pool_ptr, logger_ptr);
    auth::add_userrights(router, pool_ptr, logger_ptr);
    auth::am_i_authed(router, pool_ptr, logger_ptr);
    auth::am_i_admin(router, pool_ptr, logger_ptr);
    auth::enable_delete(router, pool_ptr, logger_ptr);

    // enable_all_actives(router, pool_ptr);

    return router;
}



int main()
{
    std::shared_ptr<cp::connection_pool> pool_ptr = std::make_shared<cp::connection_pool>(Config::giveMe().sql_config);

    auto router = create(pool_ptr);

    struct traits: public default_traits_t
    {
        using request_handler_t = restinio::router::express_router_t<>;
    };
    
    restinio::run(
			restinio::on_thread_pool<traits>(7)
				.address( "localhost" )
                // .port( 8080 )
                // .buffer_size( 2048 )
				.request_handler( move(router))
    );
    return 0;
}