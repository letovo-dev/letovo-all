#include "url_parser.h"
#include <iostream>
namespace url {
bool is_number(const std::string &s) {
  return !s.empty() && std::find_if(s.begin(), s.end(), [](unsigned char c) {
                         return !std::isdigit(c);
                       }) == s.end();
}

int last_int_from_url_path(restinio::string_view_t path) {
  return std::stoi(get_last_url_arg(path));
}

std::string get_last_url_arg(restinio::string_view_t path) {
  std::string url = {path.begin(), path.end()};
  size_t pos = url.find_last_of('/');
  if (pos != std::string::npos && pos + 1 < url.size()) {
    return url.substr(pos + 1);
  }
  return "";
}

std::string get_string_after(restinio::string_view_t path,
                             const std::string &delimiter) {
  std::string s = {path.begin(), path.end()};
  size_t pos = s.find(delimiter);
  if (pos != std::string::npos) {
    return s.substr(pos + delimiter.size());
  }
  return "";
}
std::vector<std::string> spilt_url_path(std::string s,
                                        const std::string delimiter) {
  std::vector<std::string> tokens;
  size_t pos = 0;
  std::string token;
  while ((pos = s.find(delimiter)) != std::string::npos) {
    token = s.substr(0, pos);
    tokens.push_back(token);
    s.erase(0, pos + delimiter.length());
  }
  tokens.push_back(s);

  return tokens;
}
std::vector<std::string> spilt_url_path(restinio::string_view_t path,
                                        const std::string delimiter) {
  std::string s = {path.begin(), path.end()};
  return spilt_url_path(s, delimiter);
}

bool validate_pic_path(const std::string &pic_path) {
  bool format = pic_path.find(".png") != std::string::npos ||
                pic_path.find(".jpg") != std::string::npos ||
                pic_path.find(".jpeg") != std::string::npos;
  bool path = true;
  path = std::filesystem::exists(
      Config::giveMe().pages_config.achivements_path.absolute + pic_path);
  std::cout << Config::giveMe().pages_config.achivements_path.absolute +
                   pic_path
            << std::endl;
  std::cout << format && path << '\n';
  return format && path;
}

} // namespace url
