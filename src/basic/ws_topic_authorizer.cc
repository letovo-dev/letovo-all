#include "ws_topic_authorizer.h"

namespace ws {

void TopicAuthorizer::register_prefix(std::string_view prefix, check_fn check) {
    for (auto& r : rules_) {
        if (r.prefix == prefix) {
            r.fn = std::move(check);
            return;
        }
    }
    rules_.push_back(Rule{std::string{prefix}, std::move(check)});
}

AuthDecision TopicAuthorizer::check(const std::string& username,
                                    const topic_t& topic) const {
    for (const auto& r : rules_) {
        if (topic.compare(0, r.prefix.size(), r.prefix) == 0) {
            return r.fn(username, topic);
        }
    }
    return AuthDecision{false, "unknown_topic"};
}

} // namespace ws
