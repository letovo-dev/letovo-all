#include <restinio/all.hpp>
#include "rapidjson/document.h"
#include <iostream>

// server functions
#include "auth.h"

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
    enable_auth_reg(router, pool_ptr, logger_ptr);
    // enable_all_actives(router, pool_ptr);

    return router;
}



int main()
{
    // create connection pool with sql config
    std::ifstream config_file("SqlConnectionConfig.json");
    std::string json((std::istreambuf_iterator<char>(config_file)), std::istreambuf_iterator<char>());

    rapidjson::Document sql_config;

    sql_config.Parse(json.c_str());
    
    cp::connection_options options;

    options.user = sql_config["user"].GetString();
    options.password = sql_config["password"].GetString();
    options.dbname = sql_config["dbname"].GetString();
    options.hostaddr = sql_config["host"].GetString();
    options.connections_count = 1;

    std::shared_ptr<cp::connection_pool> pool_ptr = std::make_shared<cp::connection_pool>(options);

    auto router = create(pool_ptr);

    struct traits: public default_traits_t
    {
        using request_handler_t = restinio::router::express_router_t<>;
    };
    
    restinio::run(
			restinio::on_thread_pool<traits>(7)
				.address( "localhost" )
				.request_handler( move(router))
    );
    return 0;
}