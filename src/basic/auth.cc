#include "./auth.h"
#include "../market/transactions.h"

#include <stdexcept>

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

bool auth_migrations_ready(std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  return password_metadata_columns_exist(pool_ptr) &&
         user_sessions_columns_exist(pool_ptr);
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
  static const std::unordered_set<std::string> allowed = {"write_posts", "admin", "moder", "main_page"};
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

    rapidjson::Document new_body;
    new_body.Parse(req->body().c_str());

    if (new_body.HasMember("login") && new_body.HasMember("password")) {
      std::string loginHeader = new_body["login"].GetString();
      std::string passwordHeader = new_body["password"].GetString();

      if (!auth::auth(loginHeader, passwordHeader, pool_ptr)) {
        return req->create_response(restinio::status_unauthorized())
            .append_header_date_field()
            .connection_close()
            .done();
      } else {
        if (!user_sessions_columns_exist(pool_ptr)) {
          logger_ptr->error([] {
            return "auth session table is missing or incomplete; cannot create "
                   "login session";
          });
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
        auto responce = req->create_response()
                            .set_body(login_body)
                            .append_header("Authorization", token)
                            .append_header(restinio::http_field::set_cookie,
                                           security::auth_session_cookie(token))
                            .append_header("Content-Type",
                                           "application/json; charset=utf-8");

        return responce.done();
      }
    }

    else {
      return req
          ->create_response(restinio::status_non_authoritative_information())
          .append_header_date_field()
          .connection_close()
          .done();
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
        if (auth::is_rights_by_username(username, pool_ptr, "moder") ||
            auth::is_rights_by_username(username, pool_ptr, "admin")) {
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
