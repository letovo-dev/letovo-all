#include "chat_ws.h"
#include "chat.h"

#include <string_view>

namespace chat::ws {

std::string topic_chat_pair(const std::string& a, const std::string& b) {
    const std::string& lo = (a < b) ? a : b;
    const std::string& hi = (a < b) ? b : a;
    return "chat:" + lo + ":" + hi;
}

static ::ws::AuthDecision inbox_owner_only(const std::string& me,
                                           const ::ws::topic_t& topic) {
    constexpr std::string_view prefix = "inbox:";
    if (topic.size() <= prefix.size())
        return {false, "malformed inbox topic"};
    auto user = topic.substr(prefix.size());
    if (user == me) return {true, ""};
    return {false, "inbox not yours"};
}

static ::ws::AuthDecision chat_pair_check(
        const std::string& me,
        const ::ws::topic_t& topic,
        std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
    constexpr std::string_view prefix = "chat:";
    if (topic.size() <= prefix.size())
        return {false, "malformed chat topic"};
    auto rest = topic.substr(prefix.size());
    auto sep  = rest.find(':');
    if (sep == std::string::npos)
        return {false, "malformed chat topic"};
    auto a = rest.substr(0, sep);
    auto b = rest.substr(sep + 1);
    if (a.empty() || b.empty())
        return {false, "malformed chat topic"};
    if (!(a < b))
        return {false, "non-canonical pair"};
    const std::string* peer = nullptr;
    if (a == me)      peer = &b;
    else if (b == me) peer = &a;
    else              return {false, "not a participant"};
    if (!chat::can_chat(me, *peer, pool_ptr))
        return {false, "chat not allowed"};
    return {true, ""};
}

void register_topic_rules(std::shared_ptr<::ws::TopicAuthorizer> authorizer,
                          std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
    authorizer->register_prefix("inbox:", &inbox_owner_only);
    authorizer->register_prefix(
        "chat:",
        [pool_ptr](const std::string& me, const ::ws::topic_t& topic) {
            return chat_pair_check(me, topic, pool_ptr);
        });
}

} // namespace chat::ws
