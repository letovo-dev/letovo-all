#include "security.h"

#include <array>
#include <functional>
#include <iomanip>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <pqxx/pqxx>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

constexpr std::size_t kPasswordSaltBytes = 16;
constexpr std::size_t kPbkdf2Bytes = 32;
constexpr std::size_t kSessionTokenBytes = 32;

std::string bytes_to_hex(const unsigned char *data, std::size_t size) {
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (std::size_t i = 0; i < size; ++i) {
    out << std::setw(2) << static_cast<int>(data[i]);
  }
  return out.str();
}

bool constant_time_equal(const std::string &left, const std::string &right) {
  if (left.size() != right.size()) {
    return false;
  }
  return CRYPTO_memcmp(left.data(), right.data(), left.size()) == 0;
}

std::string legacy_hash(const std::string &password) {
  return std::to_string(std::hash<std::string>{}(password));
}

std::string pbkdf2_sha256(const std::string &password, const std::string &salt,
                          int iterations) {
  if (iterations <= 0) {
    throw std::invalid_argument("PBKDF2 iterations must be positive");
  }

  std::array<unsigned char, kPbkdf2Bytes> hash{};
  if (PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()),
                        reinterpret_cast<const unsigned char *>(salt.data()),
                        static_cast<int>(salt.size()), iterations, EVP_sha256(),
                        static_cast<int>(hash.size()), hash.data()) != 1) {
    throw std::runtime_error("PKCS5_PBKDF2_HMAC failed");
  }
  return bytes_to_hex(hash.data(), hash.size());
}

std::string sha256_hex(const std::string &value) {
  std::array<unsigned char, SHA256_DIGEST_LENGTH> hash{};
  SHA256(reinterpret_cast<const unsigned char *>(value.data()), value.size(),
         hash.data());
  return bytes_to_hex(hash.data(), hash.size());
}

std::string cookie_value(const std::string &cookie_header,
                         const std::string &name) {
  std::size_t start = 0;
  while (start < cookie_header.size()) {
    while (start < cookie_header.size() &&
           (cookie_header[start] == ' ' || cookie_header[start] == ';')) {
      ++start;
    }

    std::size_t end = cookie_header.find(';', start);
    if (end == std::string::npos) {
      end = cookie_header.size();
    }

    std::string part = cookie_header.substr(start, end - start);
    std::size_t eq = part.find('=');
    if (eq != std::string::npos && part.substr(0, eq) == name) {
      return part.substr(eq + 1);
    }

    start = end + 1;
  }
  return "";
}

bool has_any_role(const std::string &username,
                  std::shared_ptr<cp::ConnectionsManager> pool_ptr,
                  const std::string &role_sql) {
  if (username.empty()) {
    return false;
  }

  std::vector<std::string> params = {username};
  cp::SafeCon con{pool_ptr};
  pqxx::result result = con->execute_params(
      "SELECT 1 FROM public.\"user\" u "
      "LEFT JOIN public.role r ON r.username = u.username "
      "WHERE u.username = ($1) AND (" +
          role_sql +
          ") LIMIT 1;",
      params);
  return !result.empty();
}

} // namespace

namespace security {

std::string random_hex(std::size_t bytes) {
  std::vector<unsigned char> buffer(bytes);
  if (bytes > 0 && RAND_bytes(buffer.data(), static_cast<int>(buffer.size())) != 1) {
    throw std::runtime_error("RAND_bytes failed");
  }
  return bytes_to_hex(buffer.data(), buffer.size());
}

std::string sha256_hex(const std::string &value) { return ::sha256_hex(value); }

PasswordHash hash_password(const std::string &password) {
  return hash_password(password, kPbkdf2Sha256, random_hex(kPasswordSaltBytes),
                       kDefaultPasswordIterations);
}

PasswordHash hash_password(const std::string &password,
                           const std::string &algo,
                           const std::string &salt,
                           int iterations) {
  if (algo == kLegacyStdHash) {
    return {legacy_hash(password), "", kLegacyStdHash, 0};
  }
  if (algo != kPbkdf2Sha256) {
    throw std::invalid_argument("unsupported password hash algorithm");
  }

  std::string normalized_salt = salt.empty() ? random_hex(kPasswordSaltBytes) : salt;
  int normalized_iterations =
      iterations > 0 ? iterations : kDefaultPasswordIterations;
  return {pbkdf2_sha256(password, normalized_salt, normalized_iterations),
          normalized_salt, kPbkdf2Sha256, normalized_iterations};
}

bool verify_password(const std::string &password,
                     const std::string &expected_hash,
                     const std::string &algo,
                     const std::string &salt,
                     int iterations) {
  if (algo == kLegacyStdHash) {
    return constant_time_equal(legacy_hash(password), expected_hash);
  }
  if (algo != kPbkdf2Sha256 || salt.empty() || iterations <= 0) {
    return false;
  }

  return constant_time_equal(pbkdf2_sha256(password, salt, iterations),
                             expected_hash);
}

std::string bearer_or_cookie_token(const restinio::http_request_header_t &header) {
  try {
    std::string bearer = header.get_field("Bearer");
    if (!bearer.empty()) {
      constexpr std::string_view prefix = "Bearer ";
      if (bearer.rfind(prefix, 0) == 0) {
        return bearer.substr(prefix.size());
      }
      return bearer;
    }
  } catch (const std::exception &) {
  }

  try {
    return cookie_value(header.get_field("Cookie"), "AuthSession");
  } catch (const std::exception &) {
    return "";
  }
}

std::string create_session(const std::string &username,
                           std::shared_ptr<cp::ConnectionsManager> pool_ptr,
                           const std::string &useragent,
                           const std::string &ip_address,
                           int ttl_seconds) {
  int normalized_ttl = ttl_seconds > 0 ? ttl_seconds : kDefaultSessionTtlSeconds;

  for (int attempt = 0; attempt < 3; ++attempt) {
    std::string raw_session_id = random_hex(kSessionTokenBytes);
    std::string session_id_hash = sha256_hex(raw_session_id);
    std::vector<std::string> params = {
        session_id_hash, username, std::to_string(normalized_ttl), useragent,
        ip_address};

    cp::SafeCon con{pool_ptr};
    try {
      con->execute_params(
          "INSERT INTO public.user_sessions "
          "(session_id, username, expires_at, useragent, ip_address) "
          "VALUES (($1), ($2), now() + (($3)::integer * interval '1 second'), "
          "NULLIF(($4), ''), NULLIF(($5), ''));",
          params, true);
      return raw_session_id;
    } catch (const pqxx::unique_violation &) {
    }
  }

  throw std::runtime_error("failed to create unique session id");
}

std::string username_from_session(
    const std::string &session_id,
    std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  if (session_id.empty()) {
    return "";
  }

  std::vector<std::string> params = {sha256_hex(session_id)};
  cp::SafeCon con{pool_ptr};
  pqxx::result result = con->execute_params(
      "SELECT s.username FROM public.user_sessions s "
      "JOIN public.\"user\" u ON u.username = s.username "
      "WHERE s.session_id = ($1) AND s.revoked_at IS NULL "
      "AND s.expires_at > now() AND u.active = true;",
      params);
  if (result.empty()) {
    return "";
  }
  return result[0]["username"].as<std::string>();
}

bool revoke_session(const std::string &session_id,
                    std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  if (session_id.empty()) {
    return false;
  }

  std::vector<std::string> params = {sha256_hex(session_id)};
  cp::SafeCon con{pool_ptr};
  pqxx::result result = con->execute_params(
      "UPDATE public.user_sessions SET revoked_at = now() "
      "WHERE session_id = ($1) AND revoked_at IS NULL RETURNING session_id;",
      params, true);
  return !result.empty();
}

bool is_same_user_or_admin(const std::string &requester_username,
                           const std::string &target_username,
                           std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  if (requester_username.empty() || target_username.empty()) {
    return false;
  }
  return requester_username == target_username ||
         has_any_role(requester_username, pool_ptr,
                      "COALESCE(r.admin, false) = true OR "
                      "COALESCE(u.userrights, '') = 'admin'");
}

bool can_read_secret_posts(const std::string &username,
                           std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  return has_any_role(username, pool_ptr,
                      "COALESCE(r.admin, false) = true OR "
                      "COALESCE(r.write_posts, false) = true OR "
                      "COALESCE(r.moder, false) = true OR "
                      "COALESCE(u.userrights, '') = 'admin'");
}

std::string create_post_reveal_token(
    int post_id, const std::string &created_by,
    std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  if (post_id <= 0 || created_by.empty()) {
    throw std::invalid_argument("post reveal token requires post id and actor");
  }

  std::string raw_token = random_hex(32);
  std::vector<std::string> params = {sha256_hex(raw_token),
                                     std::to_string(post_id), created_by};
  cp::SafeCon con{pool_ptr};
  pqxx::result result = con->execute_params(
      "INSERT INTO public.post_reveal_tokens(token_hash, post_id, created_by) "
      "SELECT ($1), p.post_id, ($3) FROM public.posts p "
      "WHERE p.post_id = ($2) "
      "RETURNING token_hash;",
      params, true);
  if (result.empty()) {
    throw std::runtime_error("post not found");
  }
  return raw_token;
}

std::optional<int> post_id_from_reveal_token(
    const std::string &token, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  if (token.empty()) {
    return std::nullopt;
  }

  std::vector<std::string> params = {sha256_hex(token)};
  cp::SafeCon con{pool_ptr};
  pqxx::result result = con->execute_params(
      "UPDATE public.post_reveal_tokens "
      "SET used_at = COALESCE(used_at, now()) "
      "WHERE token_hash = ($1) "
      "AND (expires_at IS NULL OR expires_at > now()) "
      "RETURNING post_id;",
      params, true);
  if (result.empty()) {
    return std::nullopt;
  }
  return result[0]["post_id"].as<int>();
}

std::string auth_session_cookie(const std::string &session_id,
                                int ttl_seconds) {
  int normalized_ttl = ttl_seconds > 0 ? ttl_seconds : kDefaultSessionTtlSeconds;
  return "AuthSession=" + session_id +
         "; Path=/; HttpOnly; Secure; SameSite=Strict; Max-Age=" +
         std::to_string(normalized_ttl) + ";";
}

std::string expired_auth_session_cookie() {
  return "AuthSession=; Path=/; HttpOnly; Secure; SameSite=Strict; "
         "Max-Age=0;";
}

} // namespace security
