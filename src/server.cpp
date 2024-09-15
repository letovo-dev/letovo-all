#include <pqxx/pqxx>
#include <httplib.h>
#include "pqxx_cp.cc"
#include "rapidjson/document.h" 
#include "auth.cc"


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
    
    cp::connection_pool pool{options};

    enable_auth(p, pool);

    // test_http(pool);
    

    p -> listen("0.0.0.0", 8080);
}