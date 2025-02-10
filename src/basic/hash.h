#pragma once

#include <iostream>
#include <unordered_map>
#include <chrono>
#include <openssl/sha.h>
#include <iomanip>
#include <sstream>
#include <unordered_set>
#include "config.h"

using namespace std;

extern int EXPIRATION_TIME;
extern std::unordered_map<std::string, std::pair<std::string, time_t>> hash_table;
extern std::unordered_set<std::string> new_users;

namespace hashing {
    std::string hash_from_string(const std::string& input);

    std::string string_from_hash(const std::string& hash);

    bool defele_from_hash(const std::string& hash);

    bool check_new_user(const std::string& hash);

    void add_new_user(const std::string& name);
} // namespace hashing
