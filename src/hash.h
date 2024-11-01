#pragma once

#include <iostream>
#include <unordered_map>
#include <chrono>
#include <openssl/sha.h>
#include <iomanip>
#include <sstream>

using namespace std;

extern int EXPIRATION_TIME; 
extern std::unordered_map<std::string, std::pair<std::string, time_t>> hash_table;


std::string hash_from_string(const std::string& input);

std::string string_from_hash(const std::string& hash);