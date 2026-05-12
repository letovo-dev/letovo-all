#include "ws_event_bus.h"

#include <chrono>
#include <ctime>
#include <functional>
#include <thread>
#include <fmt/format.h>
#include "config.h"
#include <pqxx/pqxx>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace ws {

EventBus::ConnState::ConnState(ConnState&& o) noexcept
    : username(std::move(o.username)),
      wsh(std::move(o.wsh)),
      topics(std::move(o.topics)),
      session_db_id(o.session_db_id),
      connected_at(o.connected_at),
      last_pong_at(o.last_pong_at),
      remote_addr(std::move(o.remote_addr)),
      pending_messages(o.pending_messages.load()) {}

EventBus::ConnState& EventBus::ConnState::operator=(ConnState&& o) noexcept {
    if (this != &o) {
        username = std::move(o.username);
        wsh = std::move(o.wsh);
        topics = std::move(o.topics);
        session_db_id = o.session_db_id;
        connected_at = o.connected_at;
        last_pong_at = o.last_pong_at;
        remote_addr = std::move(o.remote_addr);
        pending_messages.store(o.pending_messages.load());
    }
    return *this;
}

EventBus::EventBus(std::shared_ptr<restinio::shared_ostream_logger_t> logger,
                   std::shared_ptr<cp::ConnectionsManager> pool_ptr)
    : logger_(std::move(logger)), pool_(std::move(pool_ptr)) {
    start_heartbeat();
}

EventBus::~EventBus() { stop_heartbeat(); }

conn_id_t EventBus::register_connection(std::string username,
                                        ws_handle_t wsh,
                                        std::string remote_addr) {
    std::int64_t session_db_id = 0;
    try {
        cp::SafeCon con{pool_};
        std::vector<std::string> params = {username, remote_addr};
        pqxx::result r = con->execute_params(
            R"(INSERT INTO ws_session(username, remote_addr)
               VALUES($1, $2::inet) RETURNING id)",
            params);
        if (!r.empty()) {
            session_db_id = r[0][0].as<std::int64_t>();
        }
    } catch (const std::exception& e) {
        logger_->error([msg = std::string(e.what())]{
            return fmt::format("ws register: INSERT ws_session failed: {}", msg);
        });
    }

    auto id = next_id_.fetch_add(1);

    ConnState st;
    st.username     = std::move(username);
    st.wsh          = std::move(wsh);
    st.remote_addr  = std::move(remote_addr);
    st.session_db_id = session_db_id;
    st.connected_at = std::chrono::system_clock::now();
    st.last_pong_at = std::chrono::steady_clock::now();

    {
        std::unique_lock lk(mtx_);
        conns_.emplace(id, std::move(st));
    }
    return id;
}

void EventBus::unregister_connection(conn_id_t id) {
    ws_handle_t wsh;
    std::int64_t session_db_id = 0;
    {
        std::unique_lock lk(mtx_);
        auto it = conns_.find(id);
        if (it == conns_.end()) return;
        for (auto& topic : it->second.topics) {
            auto bt = by_topic_.find(topic);
            if (bt != by_topic_.end()) {
                bt->second.erase(id);
                if (bt->second.empty()) by_topic_.erase(bt);
            }
        }
        wsh = std::move(it->second.wsh);
        session_db_id = it->second.session_db_id;
        conns_.erase(it);
    }

    if (wsh) {
        try { wsh->shutdown(); } catch (...) {}
    }

    if (session_db_id != 0) {
        auto pool = pool_;
        auto logger = logger_;
        std::thread([pool, logger, session_db_id]() {
            try {
                cp::SafeCon con{pool};
                std::vector<std::string> params = {std::to_string(session_db_id)};
                con->execute_params(
                    "UPDATE ws_session SET disconnected_at = NOW() WHERE id = $1",
                    params);
            } catch (const std::exception& e) {
                logger->error([msg = std::string(e.what()), session_db_id]{
                    return fmt::format(
                        "ws unregister: UPDATE ws_session(id={}) failed: {}",
                        session_db_id, msg);
                });
            }
        }).detach();
    }
}

void EventBus::subscribe(conn_id_t id, topic_t topic) {
    std::unique_lock lk(mtx_);
    auto it = conns_.find(id);
    if (it == conns_.end()) return;
    auto inserted = it->second.topics.insert(topic).second;
    if (inserted) {
        by_topic_[topic].insert(id);
    }
}

void EventBus::unsubscribe(conn_id_t id, topic_t topic) {
    std::unique_lock lk(mtx_);
    auto it = conns_.find(id);
    if (it == conns_.end()) return;
    if (it->second.topics.erase(topic)) {
        auto bt = by_topic_.find(topic);
        if (bt != by_topic_.end()) {
            bt->second.erase(id);
            if (bt->second.empty()) by_topic_.erase(bt);
        }
    }
}

void EventBus::publish(const topic_t& topic, std::string payload) {
    std::vector<ws_handle_t> targets;
    {
        std::shared_lock lk(mtx_);
        auto bt = by_topic_.find(topic);
        if (bt == by_topic_.end()) return;
        targets.reserve(bt->second.size());
        for (auto id : bt->second) {
            auto it = conns_.find(id);
            if (it != conns_.end() && it->second.wsh) {
                targets.push_back(it->second.wsh);
            }
        }
    }
    for (auto& wsh : targets) {
        try {
            wsh->send_message(restinio::websocket::basic::final_frame,
                              restinio::websocket::basic::opcode_t::text_frame,
                              payload);
        } catch (const std::exception& e) {
            logger_->warn([msg = std::string(e.what())]{
                return fmt::format("ws publish: send_message failed: {}", msg);
            });
        }
    }
}

void EventBus::publish_to_one(conn_id_t id, std::string payload) {
    ws_handle_t target;
    {
        std::shared_lock lk(mtx_);
        auto it = conns_.find(id);
        if (it == conns_.end() || !it->second.wsh) return;
        target = it->second.wsh;
    }
    try {
        target->send_message(restinio::websocket::basic::final_frame,
                             restinio::websocket::basic::opcode_t::text_frame,
                             std::move(payload));
    } catch (const std::exception& e) {
        logger_->warn([msg = std::string(e.what())]{
            return fmt::format("ws publish_to_one: failed: {}", msg);
        });
    }
}

void EventBus::mark_alive(conn_id_t id) {
    std::unique_lock lk(mtx_);
    auto it = conns_.find(id);
    if (it == conns_.end()) return;
    it->second.last_pong_at = std::chrono::steady_clock::now();
}

std::vector<EventBus::SessionInfo> EventBus::snapshot_active() const {
    std::vector<SessionInfo> out;
    std::shared_lock lk(mtx_);
    out.reserve(conns_.size());
    for (const auto& [id, st] : conns_) {
        out.push_back(SessionInfo{
            id,
            st.session_db_id,
            st.username,
            st.connected_at,
            st.remote_addr,
            st.topics.size()});
    }
    return out;
}

std::size_t EventBus::active_connections() const {
    std::shared_lock lk(mtx_);
    return conns_.size();
}

void EventBus::recover_from_crash() {
    try {
        cp::SafeCon con{pool_};
        pqxx::result r = con->execute(
            "UPDATE ws_session SET disconnected_at = NOW() WHERE disconnected_at IS NULL");
        logger_->info([r]{
            return fmt::format("ws recover_from_crash: closed {} dangling sessions",
                               r.affected_rows());
        });
    } catch (const std::exception& e) {
        logger_->error([msg = std::string(e.what())]{
            return fmt::format("ws recover_from_crash failed: {}", msg);
        });
    }
}

static std::string iso8601_utc_now() {
    using namespace std::chrono;
    auto t  = system_clock::now();
    auto tt = system_clock::to_time_t(t);
    std::tm gmt{};
    gmtime_r(&tt, &gmt);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &gmt);
    return std::string(buf);
}

std::string make_envelope(std::string_view type,
                          std::string_view topic,
                          const rapidjson::Value& data) {
    rapidjson::Document env(rapidjson::kObjectType);
    auto& a = env.GetAllocator();

    env.AddMember("type",  rapidjson::Value(std::string(type).c_str(),  a), a);
    env.AddMember("topic", rapidjson::Value(std::string(topic).c_str(), a), a);
    env.AddMember("ts",    rapidjson::Value(iso8601_utc_now().c_str(),  a), a);

    rapidjson::Value data_copy(rapidjson::kNullType);
    data_copy.CopyFrom(data, a);
    env.AddMember("data", data_copy, a);

    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> w(sb);
    env.Accept(w);
    return std::string(sb.GetString(), sb.GetSize());
}

std::string make_envelope_error(std::string_view code,
                                std::string_view topic,
                                std::string_view reason) {
    rapidjson::Document data(rapidjson::kObjectType);
    auto& a = data.GetAllocator();
    data.AddMember("code",   rapidjson::Value(std::string(code).c_str(),   a), a);
    data.AddMember("topic",  rapidjson::Value(std::string(topic).c_str(),  a), a);
    data.AddMember("reason", rapidjson::Value(std::string(reason).c_str(), a), a);
    return make_envelope("error", topic, data);
}

void EventBus::start_heartbeat() {
    hb_io_ = std::make_unique<restinio::asio_ns::io_context>();
    hb_timer_ = std::make_unique<restinio::asio_ns::steady_timer>(*hb_io_);
    hb_thread_ = std::thread([this]() {
        hb_timer_->expires_after(std::chrono::seconds(
            Config::giveMe().ws_config.ping_interval_sec));
        std::function<void(const std::error_code&)> on_tick;
        on_tick = [this, &on_tick](const std::error_code& ec) {
            if (ec || stopping_.load()) return;
            try { hb_loop_iteration(); }
            catch (const std::exception& e) {
                logger_->error([msg = std::string(e.what())]{
                    return fmt::format("ws heartbeat: {}", msg);
                });
            }
            hb_timer_->expires_after(std::chrono::seconds(
                Config::giveMe().ws_config.ping_interval_sec));
            hb_timer_->async_wait(on_tick);
        };
        hb_timer_->async_wait(on_tick);
        hb_io_->run();
    });
}

void EventBus::stop_heartbeat() {
    stopping_.store(true);
    if (hb_io_) hb_io_->stop();
    if (hb_thread_.joinable()) hb_thread_.join();
}

void EventBus::hb_loop_iteration() {
    auto pong_timeout = std::chrono::seconds(
        Config::giveMe().ws_config.pong_timeout_sec);
    auto now = std::chrono::steady_clock::now();

    struct Target { conn_id_t id; ws_handle_t wsh; bool stale; };
    std::vector<Target> targets;
    {
        std::shared_lock lk(mtx_);
        targets.reserve(conns_.size());
        for (const auto& [id, st] : conns_) {
            if (!st.wsh) continue;
            bool stale = (now - st.last_pong_at) > pong_timeout;
            targets.push_back(Target{id, st.wsh, stale});
        }
    }

    for (auto& t : targets) {
        if (t.stale) {
            logger_->info([id = t.id]{
                return fmt::format("ws heartbeat: closing stale conn {}", id);
            });
            unregister_connection(t.id);
            continue;
        }
        try {
            t.wsh->send_message(restinio::websocket::basic::final_frame,
                                restinio::websocket::basic::opcode_t::ping_frame,
                                "hb");
        } catch (...) {
            unregister_connection(t.id);
        }
    }
}

} // namespace ws
