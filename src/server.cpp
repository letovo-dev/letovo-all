#include <pqxx/pqxx>
#include <httplib.h>
#include "letovo-soc-net/chats.cc"
#include "pqxx_cp.cc"
#include "rapidjson/document.h" 



void enable_auth(std::shared_ptr<httplib::Server> p) {
    
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
    
    cp::connection_pool pool{options};
    std::shared_ptr<cp::connection_pool> pool_ptr = std::make_shared<cp::connection_pool>(pool); 
    // enable_auth(p);
    // placeholder(p);
    

    p -> listen("0.0.0.0", 8080);
}