#include "ws_endpoint.h"

#include <fmt/format.h>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <restinio/websocket/websocket.hpp>

#include "auth.h"
#include "config.h"
#include "server_traits.h"

namespace rws = restinio::websocket::basic;

namespace ws::server {

namespace {

std::string extract_token(const restinio::request_handle_t& req) {
    std::string token = security::bearer_or_cookie_token(req->header());
    if (!token.empty()) return token;

    auto qp = restinio::parse_query(req->header().query());
    if (qp.has("token")) {
        return std::string(qp["token"]);
    }
    return {};
}

std::string remote_addr_of(const restinio::request_handle_t& req) {
    try {
        auto ep = req->remote_endpoint();
        return ep.address().to_string();
    } catch (...) {
        return {};
    }
}

struct ConnCtx {
    conn_id_t id = 0;
    std::string username;
    std::shared_ptr<EventBus> bus;
    std::shared_ptr<TopicAuthorizer> authorizer;
};

void handle_text_frame(rws::ws_handle_t wsh,
                       std::shared_ptr<ConnCtx> ctx,
                       const std::string& payload) {
    rapidjson::Document doc;
    doc.Parse(payload.c_str());
    if (!doc.IsObject() || !doc.HasMember("op") || !doc["op"].IsString()) {
        wsh->send_message(rws::final_frame, rws::opcode_t::text_frame,
                          make_envelope_error("malformed", "", "expected {op, topic}"));
        return;
    }
    std::string op = doc["op"].GetString();

    if (op == "subscribe" || op == "unsubscribe") {
        if (!doc.HasMember("topic") || !doc["topic"].IsString()) {
            wsh->send_message(rws::final_frame, rws::opcode_t::text_frame,
                make_envelope_error("malformed", "", "missing topic"));
            return;
        }
        std::string topic = doc["topic"].GetString();

        if (op == "subscribe") {
            auto d = ctx->authorizer->check(ctx->username, topic);
            if (!d.allowed) {
                std::string code = (d.reason == "unknown_topic")
                                   ? "unknown_topic" : "forbidden";
                wsh->send_message(rws::final_frame, rws::opcode_t::text_frame,
                    make_envelope_error(code, topic, d.reason));
                return;
            }
            ctx->bus->subscribe(ctx->id, topic);
            rapidjson::Document ack(rapidjson::kObjectType);
            auto& a = ack.GetAllocator();
            ack.AddMember("topic", rapidjson::Value(topic.c_str(), a), a);
            wsh->send_message(rws::final_frame, rws::opcode_t::text_frame,
                make_envelope("subscribed", topic, ack));
        } else {
            ctx->bus->unsubscribe(ctx->id, topic);
        }
        return;
    }

    wsh->send_message(rws::final_frame, rws::opcode_t::text_frame,
        make_envelope_error("unknown_op", "", op));
}

} // namespace

void register_endpoint(
    std::unique_ptr<restinio::router::express_router_t<>>& router,
    std::shared_ptr<cp::ConnectionsManager> pool_ptr,
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr,
    std::shared_ptr<EventBus> bus_ptr,
    std::shared_ptr<TopicAuthorizer> authorizer_ptr) {

    router.get()->http_get("/ws", [pool_ptr, logger_ptr, bus_ptr, authorizer_ptr]
                                  (auto req, auto) -> restinio::request_handling_status_t {
        if (!Config::giveMe().ws_config.enabled) {
            return req->create_response(restinio::status_service_unavailable())
                      .set_body(R"({"error":"ws disabled"})")
                      .append_header("Content-Type", "application/json; charset=utf-8")
                      .done();
        }

        std::string token = extract_token(req);
        if (token.empty()) {
            return req->create_response(restinio::status_unauthorized())
                      .done();
        }
        std::string username = auth::get_username(token, pool_ptr);
        if (username.empty()) {
            return req->create_response(restinio::status_unauthorized())
                      .done();
        }

        auto ctx = std::make_shared<ConnCtx>();
        ctx->bus        = bus_ptr;
        ctx->authorizer = authorizer_ptr;
        ctx->username   = username;

        try {
            auto wsh = rws::upgrade<ws::server_traits>(
                *req,
                rws::activation_t::immediate,
                [ctx, logger_ptr](auto wsh, auto m) {
                    switch (m->opcode()) {
                    case rws::opcode_t::text_frame:
                        handle_text_frame(wsh, ctx, m->payload());
                        break;
                    case rws::opcode_t::binary_frame:
                        wsh->send_message(rws::final_frame,
                            rws::opcode_t::text_frame,
                            make_envelope_error("binary_unsupported", "", ""));
                        break;
                    case rws::opcode_t::ping_frame:
                        wsh->send_message(rws::final_frame,
                            rws::opcode_t::pong_frame, m->payload());
                        break;
                    case rws::opcode_t::pong_frame:
                        ctx->bus->mark_alive(ctx->id);
                        break;
                    case rws::opcode_t::connection_close_frame:
                        ctx->bus->unregister_connection(ctx->id);
                        try { wsh->shutdown(); } catch (...) {}
                        break;
                    default:
                        break;
                    }
                });

            ctx->id = bus_ptr->register_connection(
                username, wsh, remote_addr_of(req));

            bus_ptr->subscribe(ctx->id, "inbox:" + username);

            rapidjson::Document data(rapidjson::kObjectType);
            auto& a = data.GetAllocator();
            data.AddMember("username", rapidjson::Value(username.c_str(), a), a);
            bus_ptr->publish_to_one(ctx->id, make_envelope("welcome", "", data));

            return restinio::request_accepted();
        } catch (const std::exception& e) {
            logger_ptr->warn([msg = std::string(e.what())]{
                return fmt::format("ws upgrade failed: {}", msg);
            });
            return req->create_response(restinio::status_bad_request())
                      .set_body(R"({"error":"upgrade required"})")
                      .append_header("Content-Type", "application/json; charset=utf-8")
                      .done();
        }
    });
}

void list_active_sessions(
    std::unique_ptr<restinio::router::express_router_t<>>& router,
    std::shared_ptr<cp::ConnectionsManager> pool_ptr,
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr,
    std::shared_ptr<EventBus> bus_ptr) {

    router.get()->http_get("/ws/sessions/active",
        [pool_ptr, logger_ptr, bus_ptr](auto req, auto) {
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

            auto sessions = bus_ptr->snapshot_active();

            rapidjson::Document doc(rapidjson::kObjectType);
            auto& a = doc.GetAllocator();
            doc.AddMember("count", static_cast<int64_t>(sessions.size()), a);
            rapidjson::Value arr(rapidjson::kArrayType);
            for (const auto& s : sessions) {
                rapidjson::Value o(rapidjson::kObjectType);
                o.AddMember("conn_id",       static_cast<int64_t>(s.conn_id),       a);
                o.AddMember("session_db_id", static_cast<int64_t>(s.session_db_id), a);
                o.AddMember("username",
                    rapidjson::Value(s.username.c_str(), a), a);
                {
                    auto tt = std::chrono::system_clock::to_time_t(s.connected_at);
                    std::tm gmt{};
                    gmtime_r(&tt, &gmt);
                    char buf[32];
                    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &gmt);
                    o.AddMember("connected_at",
                        rapidjson::Value(buf, a), a);
                }
                o.AddMember("remote_addr",
                    rapidjson::Value(s.remote_addr.c_str(), a), a);
                o.AddMember("subscribed_topics",
                    static_cast<int64_t>(s.subscribed_topics), a);
                arr.PushBack(o, a);
            }
            doc.AddMember("result", arr, a);

            rapidjson::StringBuffer sb;
            rapidjson::Writer<rapidjson::StringBuffer> w(sb);
            doc.Accept(w);

            return req->create_response()
                .set_body(std::string(sb.GetString(), sb.GetSize()))
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
}

} // namespace ws::server
