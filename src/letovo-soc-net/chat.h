#pragma once
#include <restinio/all.hpp>
#include <pqxx/pqxx>
#include <vector>
#include <string>
#include <fmt/format.h>
#include <rapidjson/document.h>
#include "../basic/pqxx_cp.h"
#include "../basic/auth.h"
#include "../basic/ws_event_bus.h"

namespace chat {
    bool is_chattable(const std::string& username, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    bool conversation_exists(const std::string& a, const std::string& b,
                             std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    bool can_chat(const std::string& a, const std::string& b,
                  std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    pqxx::result get_chattable_users(const std::string& current_user, bool requester_is_admin,
                                     std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    pqxx::result get_messages(const std::string& user1, const std::string& user2,
                              int limit, int offset,
                              std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    int send_message(const std::string& sender, const std::string& receiver, const std::string& text, const std::vector<std::string>& attachments, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    std::string get_message_sender(int message_id, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    std::string get_message_receiver(int message_id, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    bool delete_message(int message_id, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    bool user_exists(const std::string& username, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    void set_override(const std::string& a, const std::string& b,
                      const std::string& override_type,
                      std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    bool clear_override(const std::string& a, const std::string& b,
                        std::shared_ptr<cp::ConnectionsManager> pool_ptr);
}

namespace chat::server {
    void get_chats(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void get_chat(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void new_message(std::unique_ptr<restinio::router::express_router_t<>>& router,
                     std::shared_ptr<cp::ConnectionsManager> pool_ptr,
                     std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr,
                     std::shared_ptr<::ws::EventBus> bus_ptr);

    void delete_message(std::unique_ptr<restinio::router::express_router_t<>>& router,
                        std::shared_ptr<cp::ConnectionsManager> pool_ptr,
                        std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr,
                        std::shared_ptr<::ws::EventBus> bus_ptr);

    void set_permission(std::unique_ptr<restinio::router::express_router_t<>>& router,
                        std::shared_ptr<cp::ConnectionsManager> pool_ptr,
                        std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void clear_permission(std::unique_ptr<restinio::router::express_router_t<>>& router,
                          std::shared_ptr<cp::ConnectionsManager> pool_ptr,
                          std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
}
