#pragma once

#include "restinio/all.hpp"
namespace url{
    int int_from_url_path(restinio::string_view_t path);

    std::string get_url_arg(restinio::string_view_t path);
}