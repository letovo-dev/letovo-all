#include "url_parser.h"
namespace url{
    int int_from_url_path(restinio::string_view_t path) {
        return std::stoi(get_url_arg(path));  
    }

    std::string get_url_arg(restinio::string_view_t path) {
        std::string url = {path.begin(), path.end()};
        size_t pos = url.find_last_of('/');
        if (pos != std::string::npos && pos + 1 < url.size()) {
            return url.substr(pos + 1);
        }
        return "";
    }
}