#include "./auth.h"
#include "analytics.h"
#include "otel.h"
#include "../market/transactions.h"

#include <regex>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <stdexcept>
#include <sstream>

namespace {

/** Appends payment fields to cp::serialize output without re-parsing it (serialize() can emit invalid JSON when values contain quotes). */
std::string append_login_payments_fields(const std::string &user_serialized,
                                         const std::string &payments_json) {
  if (user_serialized.empty() || user_serialized.back() != '}') {
    return user_serialized;
  }
  std::string body = user_serialized;
  body.pop_back();
  if (payments_json.size() >= 2 && payments_json.front() == '{' &&
      payments_json.back() == '}') {
    body += ',';
    body.append(payments_json.begin() + 1, payments_json.end() - 1);
  }
  body += '}';
  return body;
}

bool password_metadata_columns_exist(
    std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  cp::SafeCon con{pool_ptr};
  std::vector<std::string> params = {};
  pqxx::result result = con->execute_params(
      "SELECT COUNT(*) AS count FROM information_schema.columns "
      "WHERE table_schema = 'public' AND table_name = 'user' "
      "AND column_name IN ('password_algo', 'password_salt', "
      "'password_iterations');",
      params);

  return !result.empty() && result[0]["count"].as<int>() == 3;
}

bool user_sessions_columns_exist(
    std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  cp::SafeCon con{pool_ptr};
  std::vector<std::string> params = {};
  pqxx::result result = con->execute_params(
      "SELECT COUNT(*) AS count FROM information_schema.columns "
      "WHERE table_schema = 'public' AND table_name = 'user_sessions' "
      "AND column_name IN ('session_id', 'username', 'expires_at', "
      "'revoked_at', 'useragent', 'ip_address');",
      params);

  return !result.empty() && result[0]["count"].as<int>() == 6;
}

bool post_reveal_tokens_columns_exist(
    std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  cp::SafeCon con{pool_ptr};
  std::vector<std::string> params = {};
  pqxx::result result = con->execute_params(
      "SELECT COUNT(*) AS count FROM information_schema.columns "
      "WHERE table_schema = 'public' AND table_name = 'post_reveal_tokens' "
      "AND column_name IN ('token_hash', 'post_id', 'created_by', "
      "'created_at', 'used_at', 'expires_at');",
      params);

  return !result.empty() && result[0]["count"].as<int>() == 6;
}

bool avatar_upload_column_exists(
    std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  cp::SafeCon con{pool_ptr};
  std::vector<std::string> params = {};
  pqxx::result result = con->execute_params(
      "SELECT COUNT(*) AS count FROM information_schema.columns "
      "WHERE table_schema = 'public' AND table_name = 'role' "
      "AND column_name = 'ava_upload';",
      params);
  return !result.empty() && result[0]["count"].as<int>() == 1;
}

bool auth_migrations_ready(std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  return password_metadata_columns_exist(pool_ptr) &&
         user_sessions_columns_exist(pool_ptr) &&
         post_reveal_tokens_columns_exist(pool_ptr) &&
         avatar_upload_column_exists(pool_ptr);
}

std::string uploader_capabilities_json(bool generic, bool avatar,
                                       const std::string &username) {
  rapidjson::Document doc;
  doc.SetObject();
  auto &allocator = doc.GetAllocator();
  doc.AddMember("status", generic ? "t" : "f", allocator);
  doc.AddMember("avatar_status", avatar ? "t" : "f", allocator);
  doc.AddMember("username",
                rapidjson::Value(username.c_str(), username.size(), allocator),
                allocator);
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);
  return std::string(buffer.GetString(), buffer.GetSize());
}

const std::regex kAdminUsernameRegex("^[A-Za-z0-9_-]{4,32}$");

bool json_bool_or_default(const rapidjson::Value &object, const char *field,
                          bool fallback) {
  if (!object.IsObject() || !object.HasMember(field)) {
    return fallback;
  }
  return object[field].IsBool() ? object[field].GetBool() : fallback;
}

std::string json_string_or_default(const rapidjson::Value &object,
                                   const char *field,
                                   const std::string &fallback = "") {
  if (!object.IsObject() || !object.HasMember(field)) {
    return fallback;
  }
  return object[field].IsString() ? object[field].GetString() : fallback;
}

int json_int_or_default(const rapidjson::Value &object, const char *field,
                        int fallback) {
  if (!object.IsObject() || !object.HasMember(field)) {
    return fallback;
  }
  return object[field].IsInt() ? object[field].GetInt() : fallback;
}

bool parse_admin_create_user_request(const rapidjson::Document &body,
                                     auth::AdminCreateUserRequest &request) {
  if (!body.IsObject()) {
    return false;
  }

  request.username = json_string_or_default(body, "username");
  request.display_name =
      json_string_or_default(body, "display_name", request.username);
  request.password = json_string_or_default(body, "password");
  request.userrights = json_string_or_default(body, "userrights", "user");
  request.role_id = json_int_or_default(body, "role_id", 0);
  request.active = json_bool_or_default(body, "active", true);
  request.registered = json_bool_or_default(body, "registered", true);
  request.chattable = json_bool_or_default(body, "chattable", false);

  if (body.HasMember("role_rights") && body["role_rights"].IsObject()) {
    const auto &rights = body["role_rights"];
    request.role_rights.write_posts =
        json_bool_or_default(rights, "write_posts", false);
    request.role_rights.admin = json_bool_or_default(rights, "admin", false);
    request.role_rights.moder = json_bool_or_default(rights, "moder", false);
    request.role_rights.main_page =
        json_bool_or_default(rights, "main_page", false);
  }

  return true;
}

std::string validate_admin_create_user_request(
    const auth::AdminCreateUserRequest &request) {
  if (!std::regex_match(request.username, kAdminUsernameRegex)) {
    return "username must match ^[A-Za-z0-9_-]{4,32}$";
  }
  if (request.password.size() < 8) {
    return "password must contain at least 8 characters";
  }
  if (request.role_id <= 0) {
    return "role_id must be a positive integer";
  }
  if (request.userrights != "user" && request.userrights != "moder" &&
      request.userrights != "public_author" && request.userrights != "admin") {
    return "userrights must be user, moder, public_author, or admin";
  }
  return "";
}

std::string json_escape_for_auth_response(const std::string &value) {
  std::string escaped;
  escaped.reserve(value.size() + 8);
  for (char ch : value) {
    if (ch == '\\') {
      escaped += "\\\\";
    } else if (ch == '"') {
      escaped += "\\\"";
    } else if (ch == '\n') {
      escaped += "\\n";
    } else if (ch == '\r') {
      escaped += "\\r";
    } else {
      escaped += ch;
    }
  }
  return escaped;
}

std::string admin_create_user_result_json(
    const auth::AdminCreateUserResult &created) {
  return fmt::format(
      R"({{"result":[{{"username":"{}","display_name":"{}","userrights":"{}","role_id":{},"active":{},"registered":{},"chattable":{}}}]}})",
      json_escape_for_auth_response(created.username),
      json_escape_for_auth_response(created.display_name),
      json_escape_for_auth_response(created.userrights), created.role_id,
      created.active ? "true" : "false", created.registered ? "true" : "false",
      created.chattable ? "true" : "false");
}

std::string error_json(const std::string &code, const std::string &message) {
  return fmt::format(R"({{"error":"{}","message":"{}"}})",
                     json_escape_for_auth_response(code),
                     json_escape_for_auth_response(message));
}

bool revoke_all_sessions_for_user(
    const std::string &username,
    std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  std::vector<std::string> params = {username};
  cp::SafeCon con{pool_ptr};
  pqxx::result result = con->execute_params(
      "UPDATE public.user_sessions SET revoked_at = now() "
      "WHERE username = ($1) AND revoked_at IS NULL RETURNING session_id;",
      params, true);

  return !result.empty();
}

} // namespace

namespace auth {
auth::UsersCash users_cash;

UsersCash::UsersCash() { users = {}; }
UsersCash::~UsersCash() { users.clear(); }

void UsersCash::add_user(std::string username) {
  std::lock_guard<std::mutex> lock(mtx);
  users.insert(username);
}

bool UsersCash::is_user(std::string username) {
  std::lock_guard<std::mutex> lock(mtx);
  return users.find(username) != users.end();
}

void UsersCash::remove_user(std::string username) {
  std::lock_guard<std::mutex> lock(mtx);
  users.erase(username);
}

std::string get_username(std::string token,
                         std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  auto username = security::username_from_session(token, pool_ptr);
  if (username.empty()) {
    return "";
  }

  users_cash.add_user(username);
  return username;
}

bool is_authed(std::string token,
               std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  return get_username(token, pool_ptr) != "";
}

bool is_authed_by_body(std::string req_body,
                       std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  rapidjson::Document new_body;
  new_body.Parse(req_body.c_str());

  if (new_body.HasMember("token")) {
    std::string token = new_body["token"].GetString();
    return is_authed(token, pool_ptr);
  }
  return false;
}

bool is_admin(std::string token,
              std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  auto username = get_username(token, pool_ptr);
  std::cout << "User is not in admin cash, checking in database..."
            << std::endl;
  return is_rights_by_username(username, pool_ptr, "admin");
}

bool is_user(std::string username,
             std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  if (users_cash.is_user(username)) {
    return true;
  }
  std::vector<std::string> params = {username};

  cp::SafeCon con{pool_ptr};

  pqxx::result result = con->execute_params(
      "SELECT * FROM \"user\" WHERE \"username\"=($1);", params);


  if (result.empty()) {
    users_cash.remove_user(username);
    return false;
  }
  users_cash.add_user(username);
  return true;
}

bool is_rights_by_username(const std::string& username,
                           std::shared_ptr<cp::ConnectionsManager> pool_ptr,
                           const std::string& rights) {
  static const std::unordered_set<std::string> allowed = {
      "write_posts", "admin", "moder", "main_page", "whireable", "ava_upload"};
  if (!allowed.count(rights)) {
    return false;
  }
  std::vector<std::string> params = { username };

  cp::SafeCon con{pool_ptr};

  pqxx::result result = con->execute_params(
      "SELECT * FROM \"role\" WHERE \"username\"=($1) AND \""+ rights + "\"=true;",
      params);


  if (result.empty()) {
    return false;
  }
  return true;
}

bool is_active(std::string username,
               std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  std::vector<std::string> params = {username};

  cp::SafeCon con{pool_ptr};

  pqxx::result result =
      con->execute_params("SELECT active FROM \"user\" WHERE \"username\"=($1) "
                          "and \"active\"=true;",
                          params);


  if (result.empty()) {
    return false;
  }
  return true;
}

bool auth(std::string username, std::string password,
          std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  if (!password_matches(username, password, pool_ptr)) {
    return false;
  }
  users_cash.add_user(username);
  return true;
}

bool password_matches(std::string username, std::string password,
                      std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  std::vector<std::string> params = {username};
  bool has_password_metadata = password_metadata_columns_exist(pool_ptr);
  cp::SafeCon con{pool_ptr};

  pqxx::result result;
  if (has_password_metadata) {
    result = con->execute_params(
        "SELECT passwdhash, COALESCE(password_algo, 'legacy_std_hash') AS "
        "password_algo, COALESCE(password_salt, '') AS password_salt, "
        "COALESCE(password_iterations, 0) AS password_iterations FROM \"user\" "
        "WHERE \"username\"=($1);",
        params);
  } else {
    result = con->execute_params(
        "SELECT passwdhash, 'legacy_std_hash' AS password_algo, '' AS "
        "password_salt, 0 AS password_iterations FROM \"user\" "
        "WHERE \"username\"=($1);",
        params);
  }

  if (result.empty()) {
    return false;
  }

  const std::string password_hash = result[0]["passwdhash"].as<std::string>();
  const std::string algo = result[0]["password_algo"].as<std::string>();
  const std::string salt = result[0]["password_salt"].as<std::string>();
  const int iterations = result[0]["password_iterations"].as<int>();

  if (!security::verify_password(password, password_hash, algo, salt,
                                 iterations)) {
    return false;
  }

  if (algo != security::kPbkdf2Sha256) {
    try {
      set_password_hash(username, password, pool_ptr);
    } catch (const std::exception &) {
    }
  }

  return true;
}

bool set_password_hash(std::string username, std::string new_password,
                       std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  if (!password_metadata_columns_exist(pool_ptr)) {
    return false;
  }

  auto password_hash = security::hash_password(new_password);
  std::vector<std::string> params = {
      password_hash.hash, password_hash.salt, password_hash.algo,
      std::to_string(password_hash.iterations), username};

  cp::SafeCon con{pool_ptr};

  pqxx::result result = con->execute_params(
      "UPDATE \"user\" SET \"passwdhash\"=($1), \"password_salt\"=($2), "
      "\"password_algo\"=($3), \"password_iterations\"=($4)::integer "
      "WHERE \"username\"=($5) RETURNING username;",
      params, true);

  return !result.empty();
}

bool reg(std::string username, std::string password, std::string userid,
         std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  if (!auth_migrations_ready(pool_ptr)) {
    throw std::runtime_error("auth/session migrations are required");
  }

  {
    std::vector<std::string> params = {username};
    cp::SafeCon con{pool_ptr};
    pqxx::result result = con->execute_params(
        "SELECT 1 FROM \"user\" WHERE \"username\"=($1);", params);
    if (!result.empty()) {
      return false;
    }
  }

  auto password_hash = security::hash_password(password);
  std::vector<std::string> params = {userid,
                                     username,
                                     password_hash.hash,
                                     password_hash.salt,
                                     password_hash.algo,
                                     std::to_string(password_hash.iterations)};

  cp::SafeCon con{pool_ptr};
  try {
    pqxx::result result = con->execute_params(
        "INSERT INTO \"user\" (userid, username, passwdhash, password_salt, "
        "password_algo, password_iterations, userrights, jointime) "
        "VALUES($1, $2, $3, $4, $5, ($6)::integer, '', now()) "
        "RETURNING username;",
        params, true);
    return !result.empty();
  } catch (const pqxx::unique_violation &e) {
    return false;
  }
  return false;
}

bool admin_create_user(const AdminCreateUserRequest &request,
                       AdminCreateUserResult &created,
                       std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  auto password_hash = security::hash_password(request.password);

  std::vector<std::string> params = {
      std::to_string(request.role_id),
      request.username,
      request.display_name.empty() ? request.username : request.display_name,
      password_hash.hash,
      password_hash.salt,
      password_hash.algo,
      std::to_string(password_hash.iterations),
      request.userrights,
      request.active ? "true" : "false",
      request.registered ? "true" : "false",
      request.chattable ? "true" : "false",
      request.role_rights.write_posts ? "true" : "false",
      request.role_rights.admin ? "true" : "false",
      request.role_rights.moder ? "true" : "false",
      request.role_rights.main_page ? "true" : "false",
  };

  cp::SafeCon con{pool_ptr};
  try {
    pqxx::result result = con->execute_params(
        R"SQL(
WITH selected_role AS (
  SELECT roleid FROM "roles" WHERE roleid = ($1)::integer
),
userid_lock AS (
  SELECT pg_advisory_xact_lock(110110)
),
next_userid AS (
  SELECT COALESCE(MAX(userid), 0) + 1 AS userid
  FROM "user", userid_lock
),
created_user AS (
  INSERT INTO "user" (
    userid, username, display_name, passwdhash, password_salt,
    password_algo, password_iterations, userrights, role, active, registered, chattable,
    jointime
  )
  SELECT
    next_userid.userid, ($2), ($3), ($4), ($5),
    ($6), ($7)::integer, ($8), selected_role.roleid, ($9)::boolean,
    ($10)::boolean, ($11)::boolean, now()
  FROM selected_role
  CROSS JOIN next_userid
  RETURNING username, display_name, userrights, role, active, registered, chattable
),
created_permission AS (
  INSERT INTO role (username, write_posts, admin, moder, main_page, ava_upload)
  SELECT
    created_user.username, ($12)::boolean, ($13)::boolean,
    ($14)::boolean, ($15)::boolean,
    COALESCE(created_user.userrights <> 'child', false)
  FROM created_user
  -- RETURNING created_user.username
  RETURNING username
),
created_user_role AS (
  INSERT INTO "useroles" (roleid, username)
  SELECT selected_role.roleid, created_user.username
  FROM created_user
  JOIN selected_role ON selected_role.roleid = created_user.role
  RETURNING roleid
)
SELECT
  created_user.username,
  created_user.display_name,
  created_user.userrights,
  created_user.role AS role_id,
  created_user.active,
  created_user.registered,
  created_user.chattable
FROM created_user
JOIN created_permission ON created_permission.username = created_user.username
JOIN created_user_role ON created_user_role.roleid = created_user.role;
)SQL",
        params, true);

    if (result.empty()) {
      return false;
    }

    created.username = result[0]["username"].as<std::string>();
    created.display_name = result[0]["display_name"].as<std::string>();
    created.userrights = result[0]["userrights"].as<std::string>();
    created.role_id = result[0]["role_id"].as<int>();
    created.active = result[0]["active"].as<bool>();
    created.registered = result[0]["registered"].as<bool>();
    created.chattable = result[0]["chattable"].as<bool>();
    users_cash.add_user(created.username);
    return true;
  } catch (const pqxx::unique_violation &e) {
    return false;
  }
}

bool delete_user(std::string username,
                 std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  std::vector<std::string> params = {username};

  cp::SafeCon con{pool_ptr};

  con->execute_params("DELETE FROM \"user\" WHERE \"username\"=($1);", params,
                      true);

  users_cash.remove_user(username);

  return true;
}

bool add_userrights(std::string username, std::string rights,
                    std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  std::vector<std::string> params = {rights, username};

  cp::SafeCon con{pool_ptr};

  con->execute_params(
      "UPDATE \"user\" SET \"userrights\"=($1) WHERE \"username\"=($2);",
      params, true);


  return true;
}

bool change_username(std::string username, std::string new_username,
                     std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  {
    std::vector<std::string> params = {new_username};
    cp::SafeCon con{pool_ptr};
    pqxx::result result = con->execute_params(
        "SELECT 1 FROM \"user\" WHERE \"username\"=($1);", params);
    if (!result.empty()) {
      return false;
    }
  }

  std::vector<std::string> params = {new_username, username};

  cp::SafeCon con{pool_ptr};

  try {
    pqxx::result result = con->execute_params(
        "UPDATE \"user\" SET \"username\"=($1) WHERE \"username\"=($2) "
        "RETURNING username;",
        params, true);
    if (result.empty()) {
      return false;
    }
  } catch (const pqxx::unique_violation &e) {
    return false;
  }

  hashing::change_username(username, new_username);

  users_cash.remove_user(username);
  users_cash.add_user(new_username);


  return true;
}

bool change_password(std::string username, std::string new_password,
                     std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  return set_password_hash(username, new_password, pool_ptr);
}

bool register_true(std::string username,
                   std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  std::vector<std::string> params = {username};

  cp::SafeCon con{pool_ptr};

  con->execute_params(
      "UPDATE \"user\" SET \"registered\"=true WHERE \"username\"=($1);",
      params, true);


  return true;
}

void save_cookie(const std::string &cookie, const std::string username,
                 const std::string useragent,
                 std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  std::vector<std::string> params = {cookie, username, useragent};

  cp::SafeCon con{pool_ptr};

  con->execute_params("INSERT INTO \"cookies\" (cookie, username, useragent) "
                      "VALUES($1, $2, $3);",
                      params, true);

}

std::string get_cookie(const std::string &header) {
  if (header.empty()) {
    return "";
  }
  if (header.find("AuthCookie=") != std::string::npos) {
    std::string cookie = header.substr(header.find("AuthCookie=") + 11);
    if (cookie.find(";") != std::string::npos) {
      cookie = cookie.substr(0, cookie.find(";"));
    }
    return cookie;
  }
  return "";
}

bool migrations_ready(std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  return auth_migrations_ready(pool_ptr);
}
} // namespace auth

namespace auth::server {
void enable_auth(
    std::unique_ptr<restinio::router::express_router_t<>> &router,
    std::shared_ptr<cp::ConnectionsManager> pool_ptr,
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
  router.get()->http_post("/auth/login", [pool_ptr, logger_ptr](auto req,
                                                                auto) {
    logger_ptr->trace([] { return "called /auth/login"; });
    std::string endpoint = req->remote_endpoint().address().to_string();
    logger_ptr->info(
        [endpoint] { return fmt::format("auth request from {}", endpoint); });

    telemetry::record_domain_event(logger_ptr, "auth.login.started",
                                   telemetry::DomainEventLevel::kInfo,
                                   "request_received");

    rapidjson::Document new_body;
    new_body.Parse(req->body().c_str());

    try {
      if (new_body.HasMember("login") && new_body["login"].IsString() &&
          new_body.HasMember("password") && new_body["password"].IsString()) {
        std::string loginHeader = new_body["login"].GetString();
        std::string passwordHeader = new_body["password"].GetString();
        const auto user_hash = telemetry::stable_attribute_hash(loginHeader);

        if (!auth::is_user(loginHeader, pool_ptr)) {
          telemetry::record_domain_event(
              logger_ptr, "auth.login_failed", telemetry::DomainEventLevel::kWarn,
              "user_not_found", {{"user.hash", user_hash}});
          return req->create_response(restinio::status_unauthorized())
              .append_header_date_field()
              .connection_close()
              .done();
        }

        if (!auth::auth(loginHeader, passwordHeader, pool_ptr)) {
          telemetry::record_domain_event(
              logger_ptr, "auth.login_failed", telemetry::DomainEventLevel::kWarn,
              "bad_credentials", {{"user.hash", user_hash}});
          return req->create_response(restinio::status_unauthorized())
              .append_header_date_field()
              .connection_close()
              .done();
        }

        // Inactive users are intentionally allowed to authenticate: the active
        // flag controls current-year account lists, not portal login access.
        if (!user_sessions_columns_exist(pool_ptr)) {
          logger_ptr->error([] {
            return "auth session table is missing or incomplete; cannot create "
                   "login session";
          });
          telemetry::record_domain_event(
              logger_ptr, "auth.login_error", telemetry::DomainEventLevel::kError,
              "session_schema_missing", {{"user.hash", user_hash}});
          return req
              ->create_response(restinio::status_internal_server_error())
              .done();
        }
        auto user = user::full_user_info(loginHeader, pool_ptr);
        std::string login_body = append_login_payments_fields(
            cp::serialize(user),
            transactions::last_incoming_outgoing_payments_json(loginHeader,
                                                               pool_ptr));
        std::string useragent = req->header().has_field("User-Agent")
                                    ? req->header().get_field("User-Agent")
                                    : "";
        auto token =
            security::create_session(loginHeader, pool_ptr, useragent, endpoint);
        analytics::record_event(loginHeader, token, endpoint, useragent, "POST",
                                "/auth/login", 200, 0, "login", "{}",
                                pool_ptr, logger_ptr);
        telemetry::record_domain_event(
            logger_ptr, "auth.login.success", telemetry::DomainEventLevel::kInfo,
            "success", {{"user.hash", user_hash}});
        auto responce = req->create_response()
                            .set_body(login_body)
                            .append_header("Authorization", token)
                            .append_header(restinio::http_field::set_cookie,
                                           security::auth_session_cookie(token))
                            .append_header("Content-Type",
                                           "application/json; charset=utf-8");

        return responce.done();
      }

      telemetry::record_domain_event(logger_ptr, "auth.login_failed",
                                     telemetry::DomainEventLevel::kWarn,
                                     "missing_credentials");
      return req->create_response(restinio::status_non_authoritative_information())
          .append_header_date_field()
          .connection_close()
          .done();
    } catch (const pqxx::broken_connection &) {
      telemetry::record_domain_event(logger_ptr, "auth.login_error",
                                     telemetry::DomainEventLevel::kError,
                                     "db_error");
      throw;
    } catch (const pqxx::sql_error &) {
      telemetry::record_domain_event(logger_ptr, "auth.login_error",
                                     telemetry::DomainEventLevel::kError,
                                     "db_error");
      throw;
    } catch (const std::exception &) {
      telemetry::record_domain_event(logger_ptr, "auth.login_error",
                                     telemetry::DomainEventLevel::kError,
                                     "unexpected_exception");
      throw;
    }
  });
}

void enable_reg(std::unique_ptr<restinio::router::express_router_t<>> &router,
                std::shared_ptr<cp::ConnectionsManager> pool_ptr,
                std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
  router.get()->http_post("/auth/reg", [pool_ptr, logger_ptr](auto req, auto) {
    logger_ptr->trace([] { return "called /auth/reg"; });
    std::string endpoint = req->remote_endpoint().address().to_string();

    logger_ptr->info(
        [endpoint] { return fmt::format("reg request from {}", endpoint); });

    rapidjson::Document new_body;
    new_body.Parse(req->body().c_str());

    if (new_body.HasMember("login") &&
        new_body.HasMember(
            "password") /*&& new_body.HasMember("temp_token")*/) {
      std::string loginHeader = new_body["login"].GetString();
      std::string passwordHeader = new_body["password"].GetString();
      // TODO: discuss, may be better to reg new user each time by hands and
      // then create qr code to change username and password std::string
      // temp_token = new_body["temp_token"].GetString();
      // if(!hashing::check_new_user(temp_token)) {
      //     return
      //     req->create_response(restinio::status_unauthorized()).done();
      // }

      try {
        std::string userid =
            std::to_string(std::hash<std::string>{}(loginHeader)).substr(0, 5);

        if (!auth::reg(loginHeader, passwordHeader, userid, pool_ptr)) {
          logger_ptr->warn([endpoint, loginHeader] {
            return fmt::format("user {} already exists", loginHeader);
          });
          return req->create_response(restinio::status_forbidden())
              .append_header("Content-Type", "application/json; charset=utf-8")
              .set_body("{status: \"username is ocupied\"}")
              .done();
        }

        auto delete_created_user = [&pool_ptr, &logger_ptr, endpoint,
                                    loginHeader](const std::string &reason) {
          try {
            auth::delete_user(loginHeader, pool_ptr);
          } catch (const std::exception &cleanup_error) {
            const std::string cleanup_message = cleanup_error.what();
            logger_ptr->error(
                [endpoint, loginHeader, reason, cleanup_message] {
                  return fmt::format(
                      "failed to clean up user {} after {} from {}: {}",
                      loginHeader, reason, endpoint, cleanup_message);
                });
          } catch (...) {
            logger_ptr->error([endpoint, loginHeader, reason] {
              return fmt::format(
                  "failed to clean up user {} after {} from {}",
                  loginHeader, reason, endpoint);
            });
          }
        };

        try {
          std::string useragent = req->header().has_field("User-Agent")
                                      ? req->header().get_field("User-Agent")
                                      : "";
          auto token = security::create_session(loginHeader, pool_ptr,
                                                useragent, endpoint);
          analytics::record_event(loginHeader, token, endpoint, useragent,
                                  "POST", "/auth/reg", 200, 0,
                                  "registration", "{}", pool_ptr, logger_ptr);
          auto response_body = cp::serialize(user::user_info(loginHeader,
                                                             pool_ptr));
          logger_ptr->info([endpoint, loginHeader] {
            return fmt::format("new user with ip {} username {}", endpoint,
                               loginHeader);
          });

          return req->create_response()
              .set_body(response_body)
              .append_header("Authorization", token)
              .append_header(restinio::http_field::set_cookie,
                             security::auth_session_cookie(token))
              .done();
        } catch (const std::exception &e) {
          const std::string error_message = e.what();
          delete_created_user("registration session setup failure");
          logger_ptr->error([endpoint, loginHeader, error_message] {
            return fmt::format(
                "registration session setup failed for user {} from {}: {}",
                loginHeader, endpoint, error_message);
          });
          return req->create_response(restinio::status_internal_server_error())
              .done();
        } catch (...) {
          delete_created_user("unknown registration session setup failure");
          logger_ptr->error([endpoint, loginHeader] {
            return fmt::format(
                "registration session setup failed for user {} from {}",
                loginHeader, endpoint);
          });
          return req->create_response(restinio::status_internal_server_error())
              .done();
        }
      } catch (const char *error_message) {
        logger_ptr->error([endpoint, error_message] {
          return fmt::format("error from {} {}", endpoint, error_message);
        });
      } catch (const std::exception &e) {
        logger_ptr->error([endpoint, e] {
          return fmt::format("error from {} {}", endpoint, e.what());
        });
        return req->create_response(restinio::status_internal_server_error())
            .done();
      }
    }
    return req
        ->create_response(restinio::status_non_authoritative_information())
        .done();
  });
}

void am_i_authed(
    std::unique_ptr<restinio::router::express_router_t<>> &router,
    std::shared_ptr<cp::ConnectionsManager> pool_ptr,
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
  router.get()->http_get(
      R"(/auth/amiauthed)", [pool_ptr, logger_ptr](auto req, auto) {
        logger_ptr->trace([] { return "called /auth/amiauthed"; });
        std::string token = security::bearer_or_cookie_token(req->header());

        if (token.empty()) {
          logger_ptr->error([] { return "token is empty"; });
          return req->create_response(restinio::status_unauthorized()).done();
        }
        if (auth::is_authed(token, pool_ptr)) {
          analytics::record_event(
              auth::get_username(token, pool_ptr), token,
              req->remote_endpoint().address().to_string(),
              req->header().has_field("User-Agent")
                  ? req->header().get_field("User-Agent")
                  : "",
              "GET", "/auth/amiauthed/", 200, 0, "", "{}", pool_ptr,
              logger_ptr);
          return req->create_response(restinio::status_ok())
              .set_body("{\"status\": \"t\"}")
              .append_header("Content-Type", "application/json; charset=utf-8")
              .done();
        } else {
          return req->create_response(restinio::status_ok())
              .set_body("{\"status\": \"f\"}")
              .append_header("Content-Type", "application/json; charset=utf-8")
              .done();
        }
      });
}

void am_i_admin(std::unique_ptr<restinio::router::express_router_t<>> &router,
                std::shared_ptr<cp::ConnectionsManager> pool_ptr,
                std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
  router.get()->http_get(
      R"(/auth/amiadmin)", [pool_ptr, logger_ptr](auto req, auto) {
        logger_ptr->trace([] { return "called /auth/amiadmin"; });
        std::string token = security::bearer_or_cookie_token(req->header());

        if (token.empty()) {
          logger_ptr->error([] { return "token is empty"; });
          return req->create_response(restinio::status_unauthorized()).done();
        }
        if (auth::is_admin(token, pool_ptr)) {
          return req->create_response(restinio::status_ok())
              .set_body("{\"status\": \"t\"}")
              .append_header("Content-Type", "application/json; charset=utf-8")
              .done();
        } else {
          return req->create_response(restinio::status_ok())
              .set_body("{\"status\": \"f\"}")
              .append_header("Content-Type", "application/json; charset=utf-8")
              .done();
        }
      });
}

void am_i_uploader(
    std::unique_ptr<restinio::router::express_router_t<>> &router,
    std::shared_ptr<cp::ConnectionsManager> pool_ptr,
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
  router.get()->http_get(
      R"(/auth/amiuploader)", [pool_ptr, logger_ptr](auto req, auto) {
        logger_ptr->trace([] { return "called /auth/amiuploader"; });
        std::string token = security::bearer_or_cookie_token(req->header());

        if (token.empty()) {
          logger_ptr->error([] { return "token is empty"; });
          return req->create_response(restinio::status_unauthorized()).done();
        }
        std::string username = auth::get_username(token, pool_ptr);
        if (username.empty()) return req->create_response(restinio::status_unauthorized()).done();
        const bool generic = auth::is_rights_by_username(username, pool_ptr, "moder") ||
                             auth::is_rights_by_username(username, pool_ptr, "admin");
        cp::SafeCon con{pool_ptr};
        std::vector<std::string> avatar_params = {username};
        const auto avatar_rows = con->execute_params(
            "SELECT 1 FROM \"user\" u LEFT JOIN \"role\" r ON r.username=u.username "
            "WHERE u.username=($1) AND u.active=true AND u.userrights <> 'child' AND "
            "(COALESCE(r.ava_upload,false)=true OR COALESCE(r.admin,false)=true OR COALESCE(r.moder,false)=true);",
            avatar_params);
        const bool avatar = !avatar_rows.empty();
        return req->create_response(restinio::status_ok())
              .set_body(uploader_capabilities_json(generic, avatar, username))
              .append_header("Content-Type", "application/json; charset=utf-8")
              .done();
      });
}

void is_user_active(
    std::unique_ptr<restinio::router::express_router_t<>> &router,
    std::shared_ptr<cp::ConnectionsManager> pool_ptr,
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
  router.get()->http_get(
      R"(/auth/isactive/:username(.*))",
      [pool_ptr, logger_ptr](auto req, auto) {
        std::string username = url::get_last_url_arg(req->header().path());

        if (auth::is_active(username, pool_ptr)) {
          return req->create_response(restinio::status_ok())
              .set_body("{\"status\": \"t\"}")
              .append_header("Content-Type", "application/json; charset=utf-8")
              .done();
        } else {
          return req->create_response(restinio::status_ok())
              .set_body("{\"status\": \"f\"}")
              .append_header("Content-Type", "application/json; charset=utf-8")
              .done();
        }
      });
}

void is_user(std::unique_ptr<restinio::router::express_router_t<>> &router,
             std::shared_ptr<cp::ConnectionsManager> pool_ptr,
             std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
  router.get()->http_get(
      R"(/auth/isuser/:username(.*))", [pool_ptr, logger_ptr](auto req, auto) {
        logger_ptr->trace([] { return "called /auth/isuser/:username"; });
        std::string username = url::get_last_url_arg(req->header().path());

        logger_ptr->info([username] {
          return fmt::format("is user request for {}", username);
        });

        if (auth::is_user(username, pool_ptr)) {
          return req->create_response(restinio::status_ok())
              .set_body("{\"status\": \"t\"}")
              .append_header("Content-Type", "application/json; charset=utf-8")
              .done();
        } else {
          return req->create_response(restinio::status_ok())
              .set_body("{\"status\": \"f\"}")
              .append_header("Content-Type", "application/json; charset=utf-8")
              .done();
        }
      });
}

void logout(std::unique_ptr<restinio::router::express_router_t<>> &router,
            std::shared_ptr<cp::ConnectionsManager> pool_ptr,
            std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
  router.get()->http_put("/auth/logout", [pool_ptr, logger_ptr](auto req, auto) {
    logger_ptr->trace([] { return "called /auth/logout"; });
    const std::string token = security::bearer_or_cookie_token(req->header());
    if (!token.empty()) {
      security::revoke_session(token, pool_ptr);
    }

    return req->create_response()
        .set_body("ok")
        .append_header(restinio::http_field::set_cookie,
                       security::expired_auth_session_cookie())
        .append_header("Content-Type", "text/plain; charset=utf-8")
        .done();
  });
}

void enable_delete(
    std::unique_ptr<restinio::router::express_router_t<>> &router,
    std::shared_ptr<cp::ConnectionsManager> pool_ptr,
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
  router.get()->http_delete(R"(/auth/delete)", [pool_ptr, logger_ptr](auto req,
                                                                      auto) {
    logger_ptr->trace([] { return "called /auth/delete"; });
    std::string token = security::bearer_or_cookie_token(req->header());

    if (token.empty()) {
      logger_ptr->error([] { return "token is empty"; });
      return req->create_response(restinio::status_unauthorized()).done();
    }

    std::string username = auth::get_username(token, pool_ptr);

    if (!username.empty()) {
      if (auth::delete_user(username, pool_ptr)) {
        security::revoke_session(token, pool_ptr);
        logger_ptr->info(
            [username] { return fmt::format("user {} deleted", username); });
        return req->create_response()
            .set_body("ok")
            .append_header("Content-Type", "text/plain; charset=utf-8")
            .done();
      } else {
        return req->create_response(restinio::status_internal_server_error())
            .done();
      }
    } else {
      return req->create_response(restinio::status_unauthorized()).done();
    }
  });
}

void add_userrights(
    std::unique_ptr<restinio::router::express_router_t<>> &router,
    std::shared_ptr<cp::ConnectionsManager> pool_ptr,
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
  router.get()->http_put(R"(/auth/add_userrights)", [pool_ptr, logger_ptr](
                                                        auto req, auto) {
    logger_ptr->trace([] { return "called /auth/add_userrights"; });
    std::string token = security::bearer_or_cookie_token(req->header());

    if (token.empty()) {
      logger_ptr->error([] { return "token is empty"; });
      return req->create_response(restinio::status_unauthorized()).done();
    }

    std::string username = auth::get_username(token, pool_ptr);

    if (!auth::is_admin(token, pool_ptr)) {
      return req->create_response(restinio::status_unauthorized())
          .append_header("Content-Type", "application/json; charset=utf-8")
          .set_body("{status: \"ты даже не гражданин!\"}")
          .done();
    }
    rapidjson::Document new_body;
    new_body.Parse(req->body().c_str());

    if (new_body.HasMember("username") && new_body.HasMember("rights")) {
      std::string user = new_body["username"].GetString();
      std::string rights = new_body["rights"].GetString();

      if (auth::add_userrights(user, rights, pool_ptr)) {
        logger_ptr->info([username, user, rights] {
          return fmt::format("user {} added rights {} to {}", username, rights,
                             user);
        });
        return req->create_response()
            .set_body("ok")
            .append_header("Content-Type", "application/json; charset=utf-8")
            .done();
      } else {
        return req->create_response(restinio::status_internal_server_error())
            .done();
      }
    } else {
      return req
          ->create_response(restinio::status_non_authoritative_information())
          .done();
    }
  });
}

void change_username(
    std::unique_ptr<restinio::router::express_router_t<>> &router,
    std::shared_ptr<cp::ConnectionsManager> pool_ptr,
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
  router.get()->http_put(R"(/auth/change_username)", [pool_ptr, logger_ptr](
                                                         auto req, auto) {
    logger_ptr->trace([] { return "called /auth/change_username"; });
    std::string token = security::bearer_or_cookie_token(req->header());

    if (token.empty()) {
      return req->create_response(restinio::status_unauthorized()).done();
    }

    std::string username = auth::get_username(token, pool_ptr);
    if (username.empty()) {
      return req->create_response(restinio::status_unauthorized()).done();
    }

    logger_ptr->info([username] {
      return fmt::format("user {} change username request", username);
    });

    rapidjson::Document new_body;
    new_body.Parse(req->body().c_str());

    if (new_body.HasMember("new_username")) {
      std::string new_username = new_body["new_username"].GetString();
      if (!auth::change_username(username, new_username, pool_ptr)) {
        return req->create_response(restinio::status_conflict())
            .done();
      }
      security::revoke_session(token, pool_ptr);
      std::string useragent = req->header().has_field("User-Agent")
                                  ? req->header().get_field("User-Agent")
                                  : "";
      std::string endpoint = req->remote_endpoint().address().to_string();
      std::string new_token =
          security::create_session(new_username, pool_ptr, useragent, endpoint);
      logger_ptr->info([username, new_username] {
        return fmt::format("user {} changed username to {}", username,
                           new_username);
      });
      return req->create_response()
          .set_body(cp::serialize(user::full_user_info(new_username, pool_ptr)))
          .append_header("Authorization", new_token)
          .append_header(restinio::http_field::set_cookie,
                         security::auth_session_cookie(new_token))
          .append_header("Content-Type", "application/json; charset=utf-8")
          .done();
    } else {
      return req
          ->create_response(restinio::status_non_authoritative_information())
          .done();
    }
  });
}
void change_password(
    std::unique_ptr<restinio::router::express_router_t<>> &router,
    std::shared_ptr<cp::ConnectionsManager> pool_ptr,
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
  router.get()->http_put("/auth/change_password", [pool_ptr,
                                                   logger_ptr](auto req, auto) {
    logger_ptr->trace([] { return "called /auth/change_password"; });
    std::string token = security::bearer_or_cookie_token(req->header());

    if (token.empty()) {
      logger_ptr->error([] { return "token is empty"; });
      return req->create_response(restinio::status_unauthorized()).done();
    }

    std::string username = auth::get_username(token, pool_ptr);
    if (username.empty()) {
      return req->create_response(restinio::status_unauthorized()).done();
    }

    rapidjson::Document new_body;
    new_body.Parse(req->body().c_str());

    if (new_body.HasMember("current_password") &&
        new_body.HasMember("new_password")) {
      std::string current_password = new_body["current_password"].GetString();
      std::string new_password = new_body["new_password"].GetString();

      if (new_password.size() < 12) {
        return req->create_response(restinio::status_bad_request()).done();
      }
      if (!auth::password_matches(username, current_password, pool_ptr)) {
        return req->create_response(restinio::status_unauthorized()).done();
      }
      if (!auth::change_password(username, new_password, pool_ptr)) {
        return req->create_response(restinio::status_internal_server_error())
            .done();
      }
      if (!revoke_all_sessions_for_user(username, pool_ptr)) {
        return req->create_response(restinio::status_internal_server_error())
            .done();
      }

      logger_ptr->info([username] {
        return fmt::format("user {} changed password", username);
      });
      return req->create_response()
          .set_body("ok")
          .append_header(restinio::http_field::set_cookie,
                         security::expired_auth_session_cookie())
          .append_header("Content-Type", "text/plain; charset=utf-8")
          .done();
    } else {
      return req->create_response(restinio::status_bad_request()).done();
    }
  });
}

void admin_create_user(
    std::unique_ptr<restinio::router::express_router_t<>> &router,
    std::shared_ptr<cp::ConnectionsManager> pool_ptr,
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
  router.get()->http_post("/auth/admin_create_user", [pool_ptr, logger_ptr](
                                                        auto req, auto) {
    logger_ptr->trace([] { return "called /auth/admin_create_user"; });

    std::string token = security::bearer_or_cookie_token(req->header());
    if (token.empty()) {
      return req->create_response(restinio::status_unauthorized()).done();
    }

    if (!auth::is_admin(token, pool_ptr)) {
      return req->create_response(restinio::status_unauthorized()).done();
    }

    rapidjson::Document body;
    body.Parse(req->body().c_str());

    auth::AdminCreateUserRequest create_request;
    if (body.HasParseError() ||
        !parse_admin_create_user_request(body, create_request)) {
      return req->create_response(restinio::status_bad_request())
          .append_header("Content-Type", "application/json; charset=utf-8")
          .set_body(
              error_json("invalid_payload", "request body must be a JSON object"))
          .done();
    }

    const std::string validation_error =
        validate_admin_create_user_request(create_request);
    if (!validation_error.empty()) {
      return req->create_response(restinio::status_bad_request())
          .append_header("Content-Type", "application/json; charset=utf-8")
          .set_body(error_json("invalid_payload", validation_error))
          .done();
    }

    auth::AdminCreateUserResult created;
    const bool ok = auth::admin_create_user(create_request, created, pool_ptr);
    if (!ok) {
      const bool username_taken =
          auth::is_user(create_request.username, pool_ptr);
      return req
          ->create_response(username_taken ? restinio::status_conflict()
                                           : restinio::status_bad_request())
          .append_header("Content-Type", "application/json; charset=utf-8")
          .set_body(username_taken
                        ? error_json("duplicate_username",
                                     "username already exists")
                        : error_json("invalid_role", "role_id does not exist"))
          .done();
    }

    logger_ptr->info([created] {
      return fmt::format("admin created user {}", created.username);
    });
    return req->create_response(restinio::status_created())
        .append_header("Content-Type", "application/json; charset=utf-8")
        .set_body(admin_create_user_result_json(created))
        .done();
  });
}

void add_new_user(
    std::unique_ptr<restinio::router::express_router_t<>> &router,
    std::shared_ptr<cp::ConnectionsManager> pool_ptr,
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
  router.get()->http_post("/auth/add_user", [pool_ptr, logger_ptr](auto req,
                                                                   auto) {
    logger_ptr->trace([] { return "called /auth/add_user"; });
    rapidjson::Document new_body;
    new_body.Parse(req->body().c_str());

    std::string token = security::bearer_or_cookie_token(req->header());

    if (token.empty()) {
      logger_ptr->error([] { return "token is empty"; });
      return req->create_response(restinio::status_unauthorized()).done();
    }

    if (!auth::is_admin(token, pool_ptr)) {
      logger_ptr->info([] { return "not admin"; });
      return req->create_response(restinio::status_unauthorized()).done();
    }
    if (new_body.HasMember("input")) {
      std::string input = new_body["input"].GetString();
      hashing::add_new_user(input);
      return req->create_response()
          .set_body("ok")
          .append_header("Content-Type", "text/plain; charset=utf-8")
          .done();
    } else {
      return req
          ->create_response(restinio::status_non_authoritative_information())
          .done();
    }
  });
}

void register_true(
    std::unique_ptr<restinio::router::express_router_t<>> &router,
    std::shared_ptr<cp::ConnectionsManager> pool_ptr,
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
  router.get()->http_put(
      "/auth/register_true", [pool_ptr, logger_ptr](auto req, auto) {
        logger_ptr->trace([] { return "called /auth/register_true"; });
        std::string token = security::bearer_or_cookie_token(req->header());

        if (token.empty()) {
          logger_ptr->error([] { return "token is empty"; });
          return req->create_response(restinio::status_unauthorized()).done();
        }

        std::string username = auth::get_username(token, pool_ptr);
        if (username.empty()) {
          return req->create_response(restinio::status_unauthorized()).done();
        }

        if (auth::register_true(username, pool_ptr)) {
          return req->create_response()
              .set_body("ok")
              .append_header("Content-Type", "text/plain; charset=utf-8")
              .done();
        } else {
          return req->create_response(restinio::status_internal_server_error())
              .done();
        }
      });
}

void is_admin(std::unique_ptr<restinio::router::express_router_t<>> &router,
              std::shared_ptr<cp::ConnectionsManager> pool_ptr,
              std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
  router.get()->http_get(
      R"(/auth/isadmin/:username(.*))", [pool_ptr, logger_ptr](auto req, auto) {
        logger_ptr->trace([] { return "called /auth/isadmin/:username"; });
        std::string username = url::get_last_url_arg(req->header().path());

        logger_ptr->info([username] {
          return fmt::format("is admin request for {}", username);
        });

        if (auth::is_rights_by_username(username, pool_ptr, "admin")) {
          return req->create_response(restinio::status_ok())
              .set_body("{\"status\": \"t\"}")
              .append_header("Content-Type", "application/json; charset=utf-8")
              .done();
        } else {
          return req->create_response(restinio::status_ok())
              .set_body("{\"status\": \"f\"}")
              .append_header("Content-Type", "application/json; charset=utf-8")
              .done();
        }
      });
}
} // namespace auth::server
