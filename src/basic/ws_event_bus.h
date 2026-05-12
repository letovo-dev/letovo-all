#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <rapidjson/document.h>
#include <restinio/all.hpp>
#include <restinio/websocket/websocket.hpp>

#include "pqxx_cp.h"
#include <thread>

namespace ws {

using conn_id_t = std::uint64_t;
using topic_t   = std::string;
using ws_handle_t = restinio::websocket::basic::ws_handle_t;

class EventBus {
public:
    EventBus(std::shared_ptr<restinio::shared_ostream_logger_t> logger,
             std::shared_ptr<cp::ConnectionsManager> pool_ptr);
    ~EventBus();

    conn_id_t register_connection(std::string username,
                                  ws_handle_t wsh,
                                  std::string remote_addr);
    void      unregister_connection(conn_id_t id);

    void subscribe   (conn_id_t id, topic_t topic);
    void unsubscribe (conn_id_t id, topic_t topic);

    void publish        (const topic_t& topic, std::string payload);
    void publish_to_one (conn_id_t id,         std::string payload);

    void mark_alive(conn_id_t id);

    struct SessionInfo {
        conn_id_t   conn_id;
        std::int64_t session_db_id;
        std::string username;
        std::chrono::system_clock::time_point connected_at;
        std::string remote_addr;
        std::size_t subscribed_topics;
    };
    std::vector<SessionInfo> snapshot_active() const;

    void recover_from_crash();

    std::size_t active_connections() const;

private:
    struct ConnState {
        std::string username;
        ws_handle_t wsh;
        std::unordered_set<topic_t> topics;
        std::int64_t session_db_id = 0;
        std::chrono::system_clock::time_point connected_at;
        std::chrono::steady_clock::time_point last_pong_at;
        std::string remote_addr;
        std::atomic<int> pending_messages{0};

        ConnState() = default;
        ConnState(ConnState&&) noexcept;
        ConnState& operator=(ConnState&&) noexcept;
        ConnState(const ConnState&) = delete;
        ConnState& operator=(const ConnState&) = delete;
    };

    std::shared_ptr<restinio::shared_ostream_logger_t> logger_;
    std::shared_ptr<cp::ConnectionsManager> pool_;

    mutable std::shared_mutex mtx_;
    std::unordered_map<conn_id_t, ConnState>                       conns_;
    std::unordered_map<topic_t,   std::unordered_set<conn_id_t>>   by_topic_;
    std::atomic<conn_id_t> next_id_{1};

    std::unique_ptr<restinio::asio_ns::io_context> hb_io_;
    std::unique_ptr<restinio::asio_ns::steady_timer> hb_timer_;
    std::thread hb_thread_;
    std::atomic<bool> stopping_{false};

    void hb_loop_iteration();
    void start_heartbeat();
    void stop_heartbeat();
};

std::string make_envelope(std::string_view type,
                          std::string_view topic,
                          const rapidjson::Value& data);

std::string make_envelope_error(std::string_view code,
                                std::string_view topic,
                                std::string_view reason);

} // namespace ws
