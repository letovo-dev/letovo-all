#include <pqxx/pqxx>
#include <httplib.h>
#include "pqxx_cp.h"
#include "rapidjson/document.h" 
#include "auth.h"
#include "market/actives_server.h"

void echo(std::shared_ptr<httplib::Server> svr_ptr) {
    svr_ptr -> Get("/hi", [](const httplib::Request & /*req*/, httplib::Response &res) {
    res.set_content("Hello World!", "text/plain");
  });
}


int main() {
    std::shared_ptr<httplib::Server> p = std::make_shared<httplib::Server>();

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

    // start all server functions
    echo(p);
    enable_auth_reg(p, pool_ptr);    
    enable_all_actives(p, pool_ptr);

    p -> listen("0.0.0.0", 8080);
}