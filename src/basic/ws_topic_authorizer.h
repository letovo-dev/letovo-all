#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ws {

using topic_t = std::string;

struct AuthDecision {
    bool        allowed;
    std::string reason;
};

class TopicAuthorizer {
public:
    using check_fn = std::function<AuthDecision(const std::string& username,
                                                const topic_t&     topic)>;

    void register_prefix(std::string_view prefix, check_fn check);

    AuthDecision check(const std::string& username, const topic_t& topic) const;

private:
    struct Rule { std::string prefix; check_fn fn; };
    std::vector<Rule> rules_;
};

} // namespace ws
