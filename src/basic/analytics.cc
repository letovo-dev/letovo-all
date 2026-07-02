#include "analytics.h"

#include "auth.h"
#include "security.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fmt/format.h>
#include <pqxx/pqxx>
#include <rapidjson/document.h>
#include <string_view>
#include <vector>

namespace {

constexpr int kMaxRouteLength = 200;
constexpr int kMaxEventLength = 64;

std::string hash_salt() {
  const char *env_salt = std::getenv("LETOVO_ACTIVITY_HASH_SALT");
  if (env_salt && env_salt[0] != '\0') {
    return env_salt;
  }
  return "letovo-user-activity-v1";
}

std::string hashed_or_empty(const std::string &value) {
  if (value.empty()) {
    return "";
  }
  return security::sha256_hex(hash_salt() + ":" + value);
}

std::string header_value(const restinio::http_request_header_t &header,
                         const std::string &name) {
  try {
    return header.get_field(name);
  } catch (const std::exception &) {
    return "";
  }
}

bool starts_with(const std::string &value, std::string_view prefix) {
  return value.rfind(prefix, 0) == 0;
}

std::string strip_query_and_fragment(std::string route) {
  std::size_t end = route.find_first_of("?#");
  if (end != std::string::npos) {
    route = route.substr(0, end);
  }
  return route.empty() ? "/" : route;
}

bool is_valid_client_event(const std::string &event) {
  if (event.empty() || event.size() > kMaxEventLength) {
    return false;
  }
  return std::all_of(event.begin(), event.end(), [](unsigned char ch) {
    return std::isalnum(ch) || ch == '_' || ch == '-' || ch == ':';
  });
}

bool is_valid_client_route(const std::string &route) {
  return !route.empty() && route.size() <= kMaxRouteLength &&
         route[0] == '/' && route.find('?') == std::string::npos &&
         route.find('#') == std::string::npos;
}

int parse_window_minutes(const std::string &window) {
  if (window == "5m") {
    return 5;
  }
  if (window == "15m") {
    return 15;
  }
  if (window == "60m" || window == "1h") {
    return 60;
  }
  return 15;
}

int parse_days(const std::string &days) {
  try {
    int parsed = std::stoi(days);
    if (parsed < 1) {
      return 1;
    }
    if (parsed > 90) {
      return 90;
    }
    return parsed;
  } catch (const std::exception &) {
    return 30;
  }
}

bool admin_allowed(const restinio::http_request_header_t &header,
                   std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  const std::string token = security::bearer_or_cookie_token(header);
  return !token.empty() && auth::is_admin(token, pool_ptr);
}

} // namespace

namespace analytics {

std::string normalize_route(const std::string &route) {
  std::string normalized = strip_query_and_fragment(route);
  if (starts_with(normalized, "/user/full/")) {
    return "/user/full/:username";
  }
  if (starts_with(normalized, "/user/")) {
    return "/user/:username";
  }
  if (starts_with(normalized, "/profile/")) {
    return "/profile/:username";
  }
  if (starts_with(normalized, "/chat/")) {
    return "/chat/:username";
  }
  if (starts_with(normalized, "/open-a/")) {
    return "/open-a/:username/:id";
  }
  if (starts_with(normalized, "/open-article/")) {
    return "/open-article/:id";
  }
  if (starts_with(normalized, "/transactions/")) {
    return "/transactions/*";
  }
  if (starts_with(normalized, "/media/get/") ||
      starts_with(normalized, "/upload/")) {
    return "";
  }
  return normalized;
}

void record_event(const std::string &username, const std::string &session_id,
                  const std::string &ip_address,
                  const std::string &user_agent, const std::string &method,
                  const std::string &route, int status, int duration_ms,
                  const std::string &client_event,
                  const std::string &metadata_json,
                  std::shared_ptr<cp::ConnectionsManager> pool_ptr,
                  std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
  const std::string normalized_route = normalize_route(route);
  if (username.empty() || normalized_route.empty()) {
    return;
  }

  std::vector<std::string> params = {
      username,
      hashed_or_empty(session_id),
      hashed_or_empty(ip_address),
      hashed_or_empty(user_agent),
      method,
      normalized_route,
      std::to_string(status),
      std::to_string(std::max(duration_ms, 0)),
      client_event,
      metadata_json.empty() ? "{}" : metadata_json};

  try {
    cp::SafeCon con{pool_ptr};
    con->execute_params(
        "INSERT INTO public.user_activity_events "
        "(username, session_id_hash, ip_hash, user_agent_hash, method, route, "
        "status, duration_ms, client_event, metadata) "
        "VALUES (($1), NULLIF(($2), ''), NULLIF(($3), ''), NULLIF(($4), ''), "
        "NULLIF(($5), ''), ($6), ($7)::integer, ($8)::integer, "
        "NULLIF(($9), ''), COALESCE(NULLIF(($10), '')::jsonb, '{}'::jsonb));",
        params, true);
    con->execute_params(
        "INSERT INTO public.user_activity_last_seen "
        "(username, last_seen_at, last_route, last_client_event, "
        "session_id_hash, ip_hash, user_agent_hash) "
        "VALUES (($1), now(), ($6), NULLIF(($9), ''), NULLIF(($2), ''), "
        "NULLIF(($3), ''), NULLIF(($4), '')) "
        "ON CONFLICT (username) DO UPDATE SET "
        "last_seen_at = EXCLUDED.last_seen_at, "
        "last_route = EXCLUDED.last_route, "
        "last_client_event = EXCLUDED.last_client_event, "
        "session_id_hash = EXCLUDED.session_id_hash, "
        "ip_hash = EXCLUDED.ip_hash, "
        "user_agent_hash = EXCLUDED.user_agent_hash;",
        params, true);
  } catch (const std::exception &e) {
    if (logger_ptr) {
      const std::string message = e.what();
      logger_ptr->warn([message] {
        return fmt::format("activity analytics write skipped: {}", message);
      });
    }
  }
}

std::string summary_json(std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  cp::SafeCon con{pool_ptr};
  pqxx::result result = con->execute(
      "WITH top_routes AS ("
      "  SELECT COALESCE(json_agg(json_build_object('route', route, 'events', "
      "events) ORDER BY events DESC), '[]'::json) AS routes "
      "  FROM ("
      "    SELECT route, COUNT(*)::integer AS events "
      "    FROM public.user_activity_events "
      "    WHERE occurred_at >= now() - interval '24 hours' "
      "    GROUP BY route ORDER BY events DESC LIMIT 10"
      "  ) ranked"
      ") "
      "SELECT json_build_object("
      "  'active_5m', (SELECT COUNT(*) FROM public.user_activity_last_seen "
      "WHERE last_seen_at >= now() - interval '5 minutes'),"
      "  'active_15m', (SELECT COUNT(*) FROM public.user_activity_last_seen "
      "WHERE last_seen_at >= now() - interval '15 minutes'),"
      "  'active_60m', (SELECT COUNT(*) FROM public.user_activity_last_seen "
      "WHERE last_seen_at >= now() - interval '60 minutes'),"
      "  'dau_today', (SELECT COUNT(DISTINCT username) FROM "
      "public.user_activity_events WHERE occurred_at::date = CURRENT_DATE),"
      "  'dau_24h', (SELECT COUNT(DISTINCT username) FROM "
      "public.user_activity_events WHERE occurred_at >= now() - interval '24 "
      "hours'),"
      "  'wau', (SELECT COUNT(DISTINCT username) FROM "
      "public.user_activity_events WHERE occurred_at >= now() - interval '7 "
      "days'),"
      "  'mau', (SELECT COUNT(DISTINCT username) FROM "
      "public.user_activity_events WHERE occurred_at >= now() - interval '30 "
      "days'),"
      "  'events_24h', (SELECT COUNT(*) FROM public.user_activity_events "
      "WHERE occurred_at >= now() - interval '24 hours'),"
      "  'top_routes_24h', (SELECT routes FROM top_routes)"
      ")::text AS body;");
  return result[0]["body"].as<std::string>();
}

std::string daily_json(int days,
                       std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  std::vector<std::string> params = {std::to_string(days)};
  cp::SafeCon con{pool_ptr};
  pqxx::result result = con->execute_params(
      "WITH daily AS ("
      "  SELECT occurred_at::date AS day, COUNT(DISTINCT username)::integer AS "
      "users, COUNT(*)::integer AS events "
      "  FROM public.user_activity_events "
      "  WHERE occurred_at >= CURRENT_DATE - (($1)::integer - 1) * interval '1 "
      "day' "
      "  GROUP BY occurred_at::date ORDER BY day"
      ") "
      "SELECT json_build_object('days', ($1)::integer, 'items', "
      "COALESCE(json_agg(json_build_object('day', day, 'users', users, "
      "'events', events) ORDER BY day), '[]'::json))::text AS body FROM daily;",
      params);
  return result[0]["body"].as<std::string>();
}

std::string active_users_json(
    int window_minutes, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  std::vector<std::string> params = {std::to_string(window_minutes)};
  cp::SafeCon con{pool_ptr};
  pqxx::result result = con->execute_params(
      "SELECT json_build_object('window_minutes', ($1)::integer, 'users', "
      "COALESCE(json_agg(json_build_object('username', username, "
      "'last_seen_at', last_seen_at, 'last_route', last_route, "
      "'last_client_event', last_client_event) ORDER BY last_seen_at DESC), "
      "'[]'::json))::text AS body "
      "FROM public.user_activity_last_seen "
      "WHERE last_seen_at >= now() - (($1)::integer * interval '1 minute');",
      params);
  return result[0]["body"].as<std::string>();
}

std::string user_activity_json(
    const std::string &username, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  std::vector<std::string> params = {username};
  cp::SafeCon con{pool_ptr};
  pqxx::result result = con->execute_params(
      "SELECT json_build_object("
      "  'username', ($1),"
      "  'last_seen', (SELECT row_to_json(ls) FROM ("
      "    SELECT last_seen_at, last_route, last_client_event FROM "
      "public.user_activity_last_seen WHERE username = ($1)"
      "  ) ls),"
      "  'events', COALESCE((SELECT json_agg(json_build_object("
      "    'occurred_at', occurred_at, 'method', method, 'route', route, "
      "'status', status, 'duration_ms', duration_ms, 'client_event', "
      "client_event) ORDER BY occurred_at DESC)"
      "    FROM (SELECT occurred_at, method, route, status, duration_ms, "
      "client_event FROM public.user_activity_events WHERE username = ($1) "
      "ORDER BY occurred_at DESC LIMIT 100) recent), '[]'::json)"
      ")::text AS body;",
      params);
  return result[0]["body"].as<std::string>();
}

} // namespace analytics

namespace analytics::server {

void register_routes(
    std::unique_ptr<restinio::router::express_router_t<>> &router,
    std::shared_ptr<cp::ConnectionsManager> pool_ptr,
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
  router.get()->http_post("/analytics/activity/ping",
                          [pool_ptr, logger_ptr](auto req, auto) {
                            const std::string token =
                                security::bearer_or_cookie_token(req->header());
                            if (token.empty()) {
                              return req
                                  ->create_response(
                                      restinio::status_unauthorized())
                                  .done();
                            }
                            const std::string username =
                                auth::get_username(token, pool_ptr);
                            if (username.empty()) {
                              return req
                                  ->create_response(
                                      restinio::status_unauthorized())
                                  .done();
                            }

                            rapidjson::Document body;
                            body.Parse(req->body().c_str());
                            if (body.HasParseError() || !body.IsObject() ||
                                !body.HasMember("event") ||
                                !body["event"].IsString() ||
                                !body.HasMember("route") ||
                                !body["route"].IsString()) {
                              return req
                                  ->create_response(restinio::status_bad_request())
                                  .done();
                            }

                            const std::string event =
                                body["event"].GetString();
                            const std::string route =
                                strip_query_and_fragment(body["route"].GetString());
                            if (!is_valid_client_event(event) ||
                                !is_valid_client_route(route)) {
                              return req
                                  ->create_response(restinio::status_bad_request())
                                  .done();
                            }

                            record_event(
                                username, token,
                                req->remote_endpoint().address().to_string(),
                                header_value(req->header(), "User-Agent"),
                                "POST", route, 200, 0, event, "{}", pool_ptr,
                                logger_ptr);

                            return req->create_response()
                                .append_header("Content-Type",
                                               "application/json; charset=utf-8")
                                .set_body("{\"status\":\"ok\"}")
                                .done();
                          });

  router.get()->http_get("/analytics/activity/summary",
                         [pool_ptr](auto req, auto) {
                           if (!admin_allowed(req->header(), pool_ptr)) {
                             return req
                                 ->create_response(
                                     restinio::status_unauthorized())
                                 .done();
                           }
                           return req->create_response()
                               .append_header("Content-Type",
                                              "application/json; charset=utf-8")
                               .set_body(summary_json(pool_ptr))
                               .done();
                         });

  router.get()->http_get("/analytics/activity/daily:search(.*)",
                         [pool_ptr](auto req, auto) {
                           if (!admin_allowed(req->header(), pool_ptr)) {
                             return req
                                 ->create_response(
                                     restinio::status_unauthorized())
                                 .done();
                           }
                           const auto qp = restinio::parse_query(
                               req->header().query());
                           const int days = qp.has("days")
                                                ? parse_days((std::string)qp["days"])
                                                : 30;
                           return req->create_response()
                               .append_header("Content-Type",
                                              "application/json; charset=utf-8")
                               .set_body(daily_json(days, pool_ptr))
                               .done();
                         });

  router.get()->http_get("/analytics/activity/active-users:search(.*)",
                         [pool_ptr](auto req, auto) {
                           if (!admin_allowed(req->header(), pool_ptr)) {
                             return req
                                 ->create_response(
                                     restinio::status_unauthorized())
                                 .done();
                           }
                           const auto qp = restinio::parse_query(
                               req->header().query());
                           const int window_minutes =
                               qp.has("window")
                                   ? parse_window_minutes((std::string)qp["window"])
                                   : 15;
                           return req->create_response()
                               .append_header("Content-Type",
                                              "application/json; charset=utf-8")
                               .set_body(
                                   active_users_json(window_minutes, pool_ptr))
                               .done();
                         });

  router.get()->http_get(
      R"(/analytics/activity/users/:username([a-zA-Z0-9_.@\-]+))",
      [pool_ptr](auto req, auto params) {
        if (!admin_allowed(req->header(), pool_ptr)) {
          return req->create_response(restinio::status_unauthorized()).done();
        }
        const std::string username = std::string(params["username"]);
        return req->create_response()
            .append_header("Content-Type", "application/json; charset=utf-8")
            .set_body(user_activity_json(username, pool_ptr))
            .done();
      });
}

} // namespace analytics::server
