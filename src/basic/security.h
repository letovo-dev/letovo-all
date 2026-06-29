#pragma once

#include "pqxx_cp.h"
#include <cstddef>
#include <memory>
#include <restinio/all.hpp>
#include <string>

namespace security {

constexpr const char *kPbkdf2Sha256 = "pbkdf2_sha256";
constexpr const char *kLegacyStdHash = "legacy_std_hash";
constexpr int kDefaultPasswordIterations = 210000;
constexpr int kDefaultSessionTtlSeconds = 864000;

struct PasswordHash {
  std::string hash;
  std::string salt;
  std::string algo = kPbkdf2Sha256;
  int iterations = kDefaultPasswordIterations;
};

std::string random_hex(std::size_t bytes);

PasswordHash hash_password(const std::string &password);
PasswordHash hash_password(const std::string &password,
                           const std::string &algo,
                           const std::string &salt,
                           int iterations);
bool verify_password(const std::string &password,
                     const std::string &expected_hash,
                     const std::string &algo,
                     const std::string &salt,
                     int iterations);

std::string bearer_or_cookie_token(const restinio::http_request_header_t &header);

std::string create_session(
    const std::string &username, std::shared_ptr<cp::ConnectionsManager> pool_ptr,
    const std::string &useragent = "", const std::string &ip_address = "",
    int ttl_seconds = kDefaultSessionTtlSeconds);
std::string username_from_session(
    const std::string &session_id,
    std::shared_ptr<cp::ConnectionsManager> pool_ptr);
bool revoke_session(const std::string &session_id,
                    std::shared_ptr<cp::ConnectionsManager> pool_ptr);

bool is_same_user_or_admin(const std::string &requester_username,
                           const std::string &target_username,
                           std::shared_ptr<cp::ConnectionsManager> pool_ptr);
bool can_read_secret_posts(const std::string &username,
                           std::shared_ptr<cp::ConnectionsManager> pool_ptr);

std::string auth_session_cookie(const std::string &session_id,
                                int ttl_seconds = kDefaultSessionTtlSeconds);
std::string expired_auth_session_cookie();

} // namespace security
