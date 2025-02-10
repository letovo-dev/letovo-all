#pragma once

#ifndef CONFIG_H
    #define CONFIG_H

    #include <fstream>
    #include "pqxx_cp.h"
    #include <filesystem>
    #include "rapidjson/document.h"

struct ServerConfig {
    std::string adress;
    int port;
    int thread_pool_size;
    std::string certs_path;
    bool update_hashes;
};

struct Path {
    std::string relative;
    std::string absolute;
};

struct PagesConfig {
    Path wiki_path;
    Path user_avatars_path;
    Path admin_avatars_path;
    Path achivements_path;
    Path media_path;
    bool create_file;
};
class Config {
public:
    cp::connection_options sql_config;
    ServerConfig server_config;
    PagesConfig pages_config;
    std::string current_path;
    std::vector<std::string> required_paths;

    static Config& giveMe()
    {
        static Config instance;
        return instance;
    }

private:
    std::string GetJson(std::string path) {
        std::ifstream config_file(path);
        std::string json((std::istreambuf_iterator<char>(config_file)), std::istreambuf_iterator<char>());
        return json;
    };

    Config() {
        current_path = std::filesystem::current_path().string();

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
        server_config.update_hashes = config_map["update_hashes"].GetBool();

        config_map.Parse(GetJson("./PagesConfig.json").c_str());
        pages_config.media_path.absolute = current_path + config_map["media_path"].GetString();
        pages_config.media_path.relative = config_map["media_path"].GetString();
        pages_config.wiki_path.absolute = pages_config.media_path.absolute + config_map["wiki_path"].GetString();
        pages_config.wiki_path.relative = config_map["wiki_path"].GetString();
        pages_config.user_avatars_path.absolute = pages_config.media_path.absolute + config_map["user_avatars_path"].GetString();
        pages_config.user_avatars_path.relative = config_map["user_avatars_path"].GetString();
        pages_config.admin_avatars_path.absolute = pages_config.media_path.absolute + config_map["admin_avatars_path"].GetString();
        pages_config.admin_avatars_path.relative = config_map["admin_avatars_path"].GetString();
        pages_config.achivements_path.absolute = pages_config.media_path.absolute + config_map["achivements_path"].GetString();
        pages_config.achivements_path.relative = config_map["achivements_path"].GetString();
        pages_config.create_file = config_map["create_file"].GetBool();

        required_paths.push_back(pages_config.wiki_path.absolute);
        required_paths.push_back(pages_config.admin_avatars_path.absolute);
    }

public:
    Config(Config const&) = delete;
    void operator=(Config const&) = delete;
};

#endif // CONFIG_H
