#include "../src/basic/avatar_policy.h"

#include <string>

namespace {

int require(bool condition, int failure) { return condition ? 0 : failure; }

} // namespace

int main() {
  const std::string owner(64, 'a');
  const std::string foreign_owner(64, 'b');
  const std::string owner_root = "/images/personal_avatars/" + owner + "/";
  const std::string owner_avatar = owner_root + "avatar.png";
  const std::string foreign_avatar =
      "/images/personal_avatars/" + foreign_owner + "/avatar.png";

  // This temporary used to leave normalize() returning a dangling string_view.
  const auto normalized =
      avatar_policy::normalize(std::string("/") + owner_avatar);
  if (const int failure = require(normalized == owner_avatar.substr(1), 1)) {
    return failure;
  }

  if (const int failure =
          require(avatar_policy::can_use_path(owner_avatar, "/images/avatars/",
                                              "/images/admin_avatars/",
                                              owner_root, false, false, true),
                  2)) {
    return failure;
  }
  if (const int failure =
          require(!avatar_policy::can_use_path(owner_avatar, "/images/avatars/",
                                               "/images/admin_avatars/",
                                               owner_root, false, false, false),
                  3)) {
    return failure;
  }
  if (const int failure =
          require(!avatar_policy::can_use_path(
                      foreign_avatar, "/images/avatars/",
                      "/images/admin_avatars/", owner_root, false, false, true),
                  4)) {
    return failure;
  }
  if (const int failure =
          require(!avatar_policy::can_use_path(
                      "/images/admin_avatars/admin.png", "/images/avatars/",
                      "/images/admin_avatars/", owner_root, false, false, true),
                  5)) {
    return failure;
  }
  if (const int failure =
          require(avatar_policy::can_use_path(
                      "/images/admin_avatars/admin.png", "/images/avatars/",
                      "/images/admin_avatars/", owner_root, true, false, false),
                  6)) {
    return failure;
  }
  if (const int failure =
          require(avatar_policy::can_use_path(
                      "/images/avatars/1.png", "/images/avatars/",
                      "/images/admin_avatars/", owner_root, false, true, false),
                  7)) {
    return failure;
  }
  if (const int failure =
          require(!avatar_policy::can_use_path(owner_avatar, "/images/avatars/",
                                               "/images/admin_avatars/",
                                               owner_root, false, true, true),
                  8)) {
    return failure;
  }
  if (const int failure =
          require(!avatar_policy::can_use_path(
                      "/images/avatars/31.png", "/images/avatars/",
                      "/images/admin_avatars/", owner_root, false, true, false),
                  9)) {
    return failure;
  }
  if (const int failure =
          require(!avatar_policy::can_use_path(
                      "/images/personal_avatars/../admin_avatars/admin.png",
                      "/images/avatars/", "/images/admin_avatars/", owner_root,
                      true, false, true),
                  10)) {
    return failure;
  }
  if (const int failure = require(
          !avatar_policy::can_use_path(
              R"(images\personal_avatars\avatar.png)", "/images/avatars/",
              "/images/admin_avatars/", owner_root, true, false, true),
          11)) {
    return failure;
  }

  return 0;
}
