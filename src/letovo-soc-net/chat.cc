#include "chat.h"
#include "chat_ws.h"
#include "../basic/ws_event_bus.h"
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include "../basic/url_parser.h"

namespace chat {

    bool is_chattable(const std::string& username, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        cp::SafeCon con{pool_ptr};
        std::vector<std::string> params = {username};
        pqxx::result result = con->execute_params(
            "SELECT \"chattable\" FROM \"user\" WHERE \"username\"=($1);", params);
        if (result.empty()) {
            return false;
        }
        return result[0]["chattable"].as<bool>();
    }

    bool conversation_exists(const std::string& a, const std::string& b,
                             std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        cp::SafeCon con{pool_ptr};
        std::vector<std::string> params = {a, b};
        pqxx::result result = con->execute_params(
            "SELECT 1 FROM direct_message "
            "WHERE ((sender = $1 AND receiver = $2) OR (sender = $2 AND receiver = $1)) "
            "  AND deleted_at IS NULL LIMIT 1;", params);
        return !result.empty();
    }

    bool has_chat_search_role(const std::string& username,
                              std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        cp::SafeCon con{pool_ptr};
        std::vector<std::string> params = {username};
        pqxx::result result = con->execute_params(
            "SELECT 1 FROM \"role\" r "
            "WHERE r.username = $1 "
            "  AND COALESCE((to_jsonb(r)->>'chat_search')::boolean, false) = true "
            "LIMIT 1;", params);
        return !result.empty();
    }

    bool can_search_all_chats(const std::string& username,
                              std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        return auth::is_rights_by_username(username, pool_ptr, "admin") ||
               has_chat_search_role(username, pool_ptr);
    }

    // `a` is the user attempting to chat with `b`. Symmetric except for the
    // privileged shortcut (admins and chat_search users may message anyone).
    // An admin-set `block` override always wins, including over privileged senders.
    bool can_chat(const std::string& a, const std::string& b,
                  std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        std::string user_a = a < b ? a : b;
        std::string user_b = a < b ? b : a;

        {
            cp::SafeCon con{pool_ptr};
            std::vector<std::string> params = {user_a, user_b};
            pqxx::result override_row = con->execute_params(
                "SELECT override_type FROM chat_override "
                "WHERE user_a = $1 AND user_b = $2;", params);
            if (!override_row.empty()) {
                std::string t = override_row[0]["override_type"].as<std::string>();
                if (t == "block") return false;
                if (t == "allow") return true;
            }
        }

        if (is_chattable(b, pool_ptr)) return true;             // chat with chattable users
        if (has_chat_search_role(b, pool_ptr)) return true;     // chat-search users are discoverable
        if (conversation_exists(a, b, pool_ptr)) return true;   // continue an existing dialog
        return can_search_all_chats(a, pool_ptr);
    }

    pqxx::result get_chattable_users(const std::string& current_user,
                                     bool requester_can_search_all,
                                     std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        cp::SafeCon con{pool_ptr};
        // $2 = requester can search all chats: admins and role.chat_search users
        // see every user they may chat with, except `block`-overridden pairs.
        std::vector<std::string> params = {current_user, requester_can_search_all ? "true" : "false"};
        pqxx::result result = con->execute_params(
            "SELECT u.username, u.display_name, u.avatar_pic, "
            "  dm.message_text AS last_message, dm.sent_at AS last_message_time "
            "FROM \"user\" u "
            "LEFT JOIN chat_override co "
            "  ON co.user_a = LEAST(u.username, $1) "
            " AND co.user_b = GREATEST(u.username, $1) "
            "LEFT JOIN \"role\" ur ON ur.username = u.username "
            "LEFT JOIN LATERAL ("
            "  SELECT message_text, sent_at FROM direct_message "
            "  WHERE ((sender = u.username AND receiver = $1) "
            "      OR (sender = $1 AND receiver = u.username)) "
            "    AND deleted_at IS NULL "
            "  ORDER BY sent_at DESC LIMIT 1"
            ") dm ON true "
            "WHERE u.username <> $1 "
            "  AND co.override_type IS DISTINCT FROM 'block' "
            "  AND ("
            "       $2::boolean "
            "    OR co.override_type = 'allow' "
            "    OR u.chattable = true "
            "    OR COALESCE((to_jsonb(ur)->>'chat_search')::boolean, false) = true "
            "    OR dm.sent_at IS NOT NULL "
            "  ) "
            "ORDER BY dm.sent_at DESC NULLS LAST, u.username;", params);
        return result;
    }

    pqxx::result get_messages(const std::string& user1, const std::string& user2,
                              int limit, int offset,
                              std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        cp::SafeCon con{pool_ptr};
        std::vector<std::string> params = {
            user1, user2, std::to_string(limit), std::to_string(offset)
        };
        pqxx::result result = con->execute_params(
            "SELECT dm.message_id, dm.sender, dm.receiver, dm.message_text, dm.sent_at, "
            "COALESCE(string_agg(ma.link, ',') FILTER (WHERE ma.link IS NOT NULL), '') AS attachments "
            "FROM direct_message dm "
            "LEFT JOIN message_attachments ma ON ma.message_id = dm.message_id "
            "WHERE ((dm.sender = $1 AND dm.receiver = $2) "
            "    OR (dm.sender = $2 AND dm.receiver = $1)) "
            "  AND dm.deleted_at IS NULL "
            "GROUP BY dm.message_id "
            "ORDER BY dm.sent_at DESC "
            "LIMIT $3 OFFSET $4;", params);
        return result;
    }

    int send_message(const std::string& sender, const std::string& receiver, const std::string& text,
                     const std::vector<std::string>& attachments, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());

        std::vector<std::string> params = {sender, receiver, text};
        pqxx::result result = con->execute_params(
            "INSERT INTO direct_message (sender, receiver, message_text) "
            "VALUES ($1, $2, $3) RETURNING message_id;", params, true);

        int message_id = result[0]["message_id"].as<int>();

        for (const auto& link : attachments) {
            std::vector<std::string> att_params = {std::to_string(message_id), link};
            con->execute_params(
                "INSERT INTO message_attachments (message_id, link) VALUES ($1, $2);",
                att_params, true);
        }

        pool_ptr->returnConnection(std::move(con));
        return message_id;
    }

    std::string get_message_sender(int message_id, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        cp::SafeCon con{pool_ptr};
        std::vector<std::string> params = {std::to_string(message_id)};
        pqxx::result result = con->execute_params(
            "SELECT sender FROM direct_message "
            "WHERE message_id = $1 AND deleted_at IS NULL;", params);
        if (result.empty()) return "";
        return result[0]["sender"].as<std::string>();
    }

    std::string get_message_receiver(int message_id, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        cp::SafeCon con{pool_ptr};
        std::vector<std::string> params = {std::to_string(message_id)};
        pqxx::result result = con->execute_params(
            "SELECT receiver FROM direct_message WHERE message_id = $1",
            params);
        if (result.empty()) return "";
        return result[0]["receiver"].as<std::string>();
    }

    bool delete_message(int message_id, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {std::to_string(message_id)};
        pqxx::result result = con->execute_params(
            "UPDATE direct_message SET deleted_at = NOW() "
            "WHERE message_id = $1 AND deleted_at IS NULL "
            "RETURNING message_id;", params, true);
        pool_ptr->returnConnection(std::move(con));
        return !result.empty();
    }

    bool user_exists(const std::string& username, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        cp::SafeCon con{pool_ptr};
        std::vector<std::string> params = {username};
        pqxx::result r = con->execute_params(
            "SELECT 1 FROM \"user\" WHERE username = $1;", params);
        return !r.empty();
    }

    void set_override(const std::string& a, const std::string& b,
                      const std::string& override_type,
                      std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        std::string user_a = a < b ? a : b;
        std::string user_b = a < b ? b : a;
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {user_a, user_b, override_type};
        con->execute_params(
            "INSERT INTO chat_override (user_a, user_b, override_type) "
            "VALUES ($1, $2, $3) "
            "ON CONFLICT (user_a, user_b) "
            "DO UPDATE SET override_type = EXCLUDED.override_type;",
            params, true);
        pool_ptr->returnConnection(std::move(con));
    }

    bool clear_override(const std::string& a, const std::string& b,
                        std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        std::string user_a = a < b ? a : b;
        std::string user_b = a < b ? b : a;
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {user_a, user_b};
        pqxx::result r = con->execute_params(
            "DELETE FROM chat_override WHERE user_a = $1 AND user_b = $2 "
            "RETURNING user_a;", params, true);
        pool_ptr->returnConnection(std::move(con));
        return !r.empty();
    }

}

namespace chat::server {

    void get_chats(std::unique_ptr<restinio::router::express_router_t<>>& router,
                   std::shared_ptr<cp::ConnectionsManager> pool_ptr,
                   std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get("/chats/", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /chats/";});
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            std::string username = auth::get_username(token, pool_ptr);
            if (username.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            bool requester_can_search_all = chat::can_search_all_chats(username, pool_ptr);
            pqxx::result result = chat::get_chattable_users(username, requester_can_search_all, pool_ptr);
            return req->create_response()
                .set_body(cp::serialize(result))
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
    }

    void get_chat(std::unique_ptr<restinio::router::express_router_t<>>& router,
                  std::shared_ptr<cp::ConnectionsManager> pool_ptr,
                  std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/chat/:username([a-zA-Z0-9\-_]+))", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /chat/:username";});
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            std::string current_user = auth::get_username(token, pool_ptr);
            if (current_user.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            std::string target_user = url::get_last_url_arg(req->header().path());
            if (target_user.empty()) {
                return req->create_response(restinio::status_bad_request()).done();
            }

            int limit = 50;
            int offset = 0;
            try {
                const auto qp = restinio::parse_query(req->header().query());
                if (qp.has("limit"))  limit  = std::stoi(std::string(qp["limit"]));
                if (qp.has("offset")) offset = std::stoi(std::string(qp["offset"]));
            } catch (const std::exception&) {
                return req->create_response(restinio::status_bad_request()).done();
            }
            if (limit  < 1)   limit  = 50;
            if (limit  > 200) limit  = 200;
            if (offset < 0)   offset = 0;

            pqxx::result result = chat::get_messages(current_user, target_user, limit, offset, pool_ptr);
            return req->create_response()
                .set_body(cp::serialize_with_shift_day(result, pool_ptr))
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
    }

    void new_message(std::unique_ptr<restinio::router::express_router_t<>>& router,
                     std::shared_ptr<cp::ConnectionsManager> pool_ptr,
                     std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr,
                     std::shared_ptr<::ws::EventBus> bus_ptr) {
        router.get()->http_post("/new_message", [pool_ptr, logger_ptr, bus_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /new_message";});
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            std::string sender = auth::get_username(token, pool_ptr);
            if (sender.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }

            rapidjson::Document body;
            body.Parse(req->body().c_str());

            if (!body.HasMember("receiver") || !body.HasMember("text")) {
                return req->create_response(restinio::status_bad_request()).done();
            }

            std::string receiver = body["receiver"].GetString();
            std::string text = body["text"].GetString();

            if (!chat::can_chat(sender, receiver, pool_ptr)) {
                return req->create_response(restinio::status_forbidden())
                    .set_body(R"({"error": "not permitted to chat with receiver"})")
                    .append_header("Content-Type", "application/json; charset=utf-8")
                    .done();
            }

            std::vector<std::string> attachments;
            if (body.HasMember("attachments") && body["attachments"].IsArray()) {
                for (auto& v : body["attachments"].GetArray()) {
                    if (v.IsString()) {
                        attachments.emplace_back(v.GetString());
                    }
                }
            }

            try {
                int message_id = chat::send_message(sender, receiver, text, attachments, pool_ptr);

                rapidjson::Document data(rapidjson::kObjectType);
                auto& a = data.GetAllocator();
                data.AddMember("message_id", message_id, a);
                data.AddMember("sender",   rapidjson::Value(sender.c_str(),   a), a);
                data.AddMember("receiver", rapidjson::Value(receiver.c_str(), a), a);
                data.AddMember("text",     rapidjson::Value(text.c_str(),     a), a);
                rapidjson::Value att_arr(rapidjson::kArrayType);
                for (const auto& s : attachments) {
                    att_arr.PushBack(rapidjson::Value(s.c_str(), a), a);
                }
                data.AddMember("attachments", att_arr, a);

                if (sender == receiver) {
                    auto inbox_topic = chat::ws::topic_inbox(receiver);
                    bus_ptr->publish(inbox_topic,
                        ::ws::make_envelope("chat.message.new", inbox_topic, data));
                } else {
                    auto inbox_topic = chat::ws::topic_inbox(receiver);
                    auto pair_topic  = chat::ws::topic_chat_pair(sender, receiver);
                    bus_ptr->publish(inbox_topic,
                        ::ws::make_envelope("chat.message.new", inbox_topic, data));
                    bus_ptr->publish(pair_topic,
                        ::ws::make_envelope("chat.message.new", pair_topic, data));
                }

                std::string response = fmt::format(
                    R"({{"message_id": {}, "sender": "{}", "receiver": "{}", "status": "sent"}})",
                    message_id, sender, receiver);
                return req->create_response()
                    .set_body(response)
                    .append_header("Content-Type", "application/json; charset=utf-8")
                    .done();
            } catch (const std::exception& e) {
                logger_ptr->error([e]{return fmt::format("error sending message: {}", e.what());});
                return req->create_response(restinio::status_internal_server_error()).done();
            }
        });
    }

    void delete_message(std::unique_ptr<restinio::router::express_router_t<>>& router,
                        std::shared_ptr<cp::ConnectionsManager> pool_ptr,
                        std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr,
                        std::shared_ptr<::ws::EventBus> bus_ptr) {
        router.get()->http_delete(R"(/chat/message/:id(\d+))", [pool_ptr, logger_ptr, bus_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called DELETE /chat/message/:id";});
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            std::string caller = auth::get_username(token, pool_ptr);
            if (caller.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }

            int message_id;
            try {
                message_id = url::last_int_from_url_path(req->header().path());
            } catch (const std::exception&) {
                return req->create_response(restinio::status_bad_request()).done();
            }
            if (message_id <= 0) {
                return req->create_response(restinio::status_bad_request()).done();
            }

            std::string sender = chat::get_message_sender(message_id, pool_ptr);
            if (sender.empty()) {
                return req->create_response(restinio::status_not_found()).done();
            }
            if (sender != caller && !auth::is_admin(token, pool_ptr)) {
                return req->create_response(restinio::status_forbidden()).done();
            }

            bool ok = chat::delete_message(message_id, pool_ptr);
            if (!ok) {
                return req->create_response(restinio::status_not_found()).done();
            }
            std::string receiver_user = chat::get_message_receiver(message_id, pool_ptr);
            if (!sender.empty() && !receiver_user.empty() && sender != receiver_user) {
                auto pair_topic = chat::ws::topic_chat_pair(sender, receiver_user);

                rapidjson::Document data(rapidjson::kObjectType);
                auto& a = data.GetAllocator();
                data.AddMember("message_id", message_id, a);
                data.AddMember("deleted_by",
                    rapidjson::Value(caller.c_str(), a), a);

                bus_ptr->publish(pair_topic,
                    ::ws::make_envelope("chat.message.deleted", pair_topic, data));
            }
            return req->create_response()
                .set_body(fmt::format(R"({{"message_id": {}, "status": "deleted"}})", message_id))
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
    }

    void set_permission(std::unique_ptr<restinio::router::express_router_t<>>& router,
                        std::shared_ptr<cp::ConnectionsManager> pool_ptr,
                        std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_post("/chat/permission", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called POST /chat/permission";});
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if (auth::get_username(token, pool_ptr).empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if (!auth::is_admin(token, pool_ptr)) {
                return req->create_response(restinio::status_forbidden()).done();
            }

            rapidjson::Document body;
            body.Parse(req->body().c_str());
            if (!body.IsObject()
                || !body.HasMember("user_a") || !body["user_a"].IsString()
                || !body.HasMember("user_b") || !body["user_b"].IsString()
                || !body.HasMember("override_type") || !body["override_type"].IsString()) {
                return req->create_response(restinio::status_bad_request()).done();
            }
            std::string user_a = body["user_a"].GetString();
            std::string user_b = body["user_b"].GetString();
            std::string override_type = body["override_type"].GetString();

            if (override_type != "allow" && override_type != "block") {
                return req->create_response(restinio::status_bad_request()).done();
            }
            if (user_a == user_b) {
                return req->create_response(restinio::status_bad_request()).done();
            }
            if (!chat::user_exists(user_a, pool_ptr) || !chat::user_exists(user_b, pool_ptr)) {
                return req->create_response(restinio::status_not_found()).done();
            }

            try {
                chat::set_override(user_a, user_b, override_type, pool_ptr);
            } catch (const std::exception& e) {
                logger_ptr->error([e]{return fmt::format("set_override failed: {}", e.what());});
                return req->create_response(restinio::status_internal_server_error()).done();
            }
            return req->create_response()
                .set_body(R"({"status":"ok"})")
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
    }

    void clear_permission(std::unique_ptr<restinio::router::express_router_t<>>& router,
                          std::shared_ptr<cp::ConnectionsManager> pool_ptr,
                          std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_delete("/chat/permission", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called DELETE /chat/permission";});
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if (auth::get_username(token, pool_ptr).empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if (!auth::is_admin(token, pool_ptr)) {
                return req->create_response(restinio::status_forbidden()).done();
            }

            rapidjson::Document body;
            body.Parse(req->body().c_str());
            if (!body.IsObject()
                || !body.HasMember("user_a") || !body["user_a"].IsString()
                || !body.HasMember("user_b") || !body["user_b"].IsString()) {
                return req->create_response(restinio::status_bad_request()).done();
            }
            std::string user_a = body["user_a"].GetString();
            std::string user_b = body["user_b"].GetString();
            if (user_a == user_b) {
                return req->create_response(restinio::status_bad_request()).done();
            }

            bool deleted = chat::clear_override(user_a, user_b, pool_ptr);
            if (!deleted) {
                return req->create_response(restinio::status_not_found()).done();
            }
            return req->create_response()
                .set_body(R"({"status":"ok"})")
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
    }

}
