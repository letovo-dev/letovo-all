#pragma once

#include <algorithm>
#include <array>
#include <string_view>

namespace avatar_policy {

inline constexpr std::string_view fallback = "images/avatars/12.png";

inline constexpr std::array<std::string_view, 30> approved_for_children = {
    "images/avatars/1.png",  "images/avatars/2.png",  "images/avatars/3.png",
    "images/avatars/4.png",  "images/avatars/5.png",  "images/avatars/6.png",
    "images/avatars/7.png",  "images/avatars/8.png",  "images/avatars/9.png",
    "images/avatars/10.png", "images/avatars/11.png", "images/avatars/12.png",
    "images/avatars/13.png", "images/avatars/14.png", "images/avatars/15.png",
    "images/avatars/16.png", "images/avatars/17.png", "images/avatars/18.png",
    "images/avatars/19.png", "images/avatars/20.png", "images/avatars/21.png",
    "images/avatars/22.png", "images/avatars/23.png", "images/avatars/24.png",
    "images/avatars/25.png", "images/avatars/26.png", "images/avatars/27.png",
    "images/avatars/28.png", "images/avatars/29.png", "images/avatars/30.png",
};

inline std::string_view normalize(std::string_view path) {
  while (!path.empty() && path.front() == '/')
    path.remove_prefix(1);
  return path;
}

inline bool is_approved_for_child(std::string_view path) {
  path = normalize(path);
  return std::find(approved_for_children.begin(), approved_for_children.end(),
                   path) != approved_for_children.end();
}

} // namespace avatar_policy
