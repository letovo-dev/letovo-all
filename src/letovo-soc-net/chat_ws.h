#pragma once

#include <memory>
#include <string>

#include "../basic/ws_topic_authorizer.h"
#include "../basic/pqxx_cp.h"

namespace chat::ws {

inline std::string topic_inbox(const std::string& user) {
    return "inbox:" + user;
}

// Returns "chat:<min(a,b)>:<max(a,b)>" — canonical pair ordering.
std::string topic_chat_pair(const std::string& a, const std::string& b);

void register_topic_rules(std::shared_ptr<::ws::TopicAuthorizer> authorizer,
                          std::shared_ptr<cp::ConnectionsManager> pool_ptr);

} // namespace chat::ws
