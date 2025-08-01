#pragma once
#include <restinio/all.hpp>
#include "pqxx_cp.h"
#include <pqxx/pqxx>
#include <unordered_set>
#include "rapidjson/document.h"
#include "asio/ip/detail/endpoint.hpp"
#include "hash.h"
#include "auth.h"
#include "comment.h"
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <fstream>
#include <filesystem>
#include "config.h"
#include "media_cash.h"

namespace media {
    extern std::unordered_map<std::string, std::string> content_types;
    
    extern std::set<std::string> allowed_no_token;

    extern media_cash::FileCache file_cache;


#define content_type(file_name) content_types[file_name]

    std::string save_file(std::string path, std::string file_name, std::string file);

    std::string get_file_type(std::string file_name);

    std::string check_if_file_exists(std::string file_name);

    std::vector<std::string> get_all_files(std::string path);

    bool is_secret(std::string file_name, std::string token, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    enum class FileStatus {
        SHARED,
        AVALUABLE,
        HACKING,
        NOT_FOUND,
        SECRET,
        UNKNOWN_TYPE,
        UNAUTHORIZED,
    };
} // namespace media

namespace media::server {
    void get_file(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void get_all_files(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
} // namespace media::server
