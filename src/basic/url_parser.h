#pragma once

#include "restinio/all.hpp"
#include <vector>
#include <string>
namespace url{
    bool is_number(const std::string& s);
    
    int last_int_from_url_path(restinio::string_view_t path);

    std::string get_last_url_arg(restinio::string_view_t path);

    std::vector<std::string> spilt_url_path(restinio::string_view_t path, const std::string delimiter);
}