#ifndef CONFIG_H
#define CONFIG_H

#include <fstream>
#include "pqxx_cp.h"
#include "rapidjson/document.h"

class ServerConfig
{
    public:
        std::string adress;
        int port;
        int thread_pool_size;
        std::string certs_path;
};
class Config
{
    public:
        cp::connection_options sql_config;
        ServerConfig server_config;

        static Config& giveMe()
        {
            static Config    instance; // Guaranteed to be destroyed.
                                  // Instantiated on first use.
            return instance;
        }
    private:
        std::string GetJson(std::string path) {
            std::ifstream config_file(path);
            std::string json((std::istreambuf_iterator<char>(config_file)), std::istreambuf_iterator<char>());
            return json;
        };

        Config() {
            
            rapidjson::Document config_map;
            config_map.Parse(GetJson("./SqlConnectionConfig.json").c_str());

            sql_config.user = config_map["user"].GetString();
            sql_config.password = config_map["password"].GetString();
            sql_config.dbname = config_map["dbname"].GetString();
            sql_config.hostaddr = config_map["host"].GetString();
            sql_config.connections_count = config_map["connections"].GetInt();

            config_map.Parse(GetJson("./ServerConfig.json").c_str());

            server_config.adress = config_map["adress"].GetString();
            server_config.port = config_map["port"].GetInt();
            server_config.thread_pool_size = config_map["thread_pool_size"].GetInt();
            server_config.certs_path = config_map["certs_path"].GetString();
        }                     

    public:
        Config(Config const&)               = delete;
        void operator=(Config const&)  = delete;
        


};

#endif // CONFIG_H
