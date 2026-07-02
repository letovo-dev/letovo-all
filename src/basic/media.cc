#include "media.h"

#include <algorithm>
#include <cstdint>
#include <sstream>

namespace {

struct ByteRange {
  std::uintmax_t start = 0;
  std::uintmax_t end = 0;
};

struct RangeRequest {
  bool present = false;
  bool valid = false;
  ByteRange range;
};

bool parse_uintmax(const std::string &value, std::uintmax_t &result) {
  if (value.empty()) {
    return false;
  }
  try {
    std::size_t pos = 0;
    unsigned long long parsed = std::stoull(value, &pos, 10);
    if (pos != value.size()) {
      return false;
    }
    result = static_cast<std::uintmax_t>(parsed);
    return true;
  } catch (...) {
    return false;
  }
}

RangeRequest parse_range_header(const std::string &range_header,
                                std::uintmax_t file_size) {
  RangeRequest request;
  if (range_header.empty()) {
    return request;
  }

  request.present = true;
  constexpr const char *prefix = "bytes=";
  if (range_header.rfind(prefix, 0) != 0 || file_size == 0 ||
      range_header.find(',') != std::string::npos) {
    return request;
  }

  std::string spec = range_header.substr(std::string(prefix).size());
  std::size_t dash = spec.find('-');
  if (dash == std::string::npos) {
    return request;
  }

  std::string start_part = spec.substr(0, dash);
  std::string end_part = spec.substr(dash + 1);
  std::uintmax_t start = 0;
  std::uintmax_t end = 0;

  if (start_part.empty()) {
    std::uintmax_t suffix_size = 0;
    if (!parse_uintmax(end_part, suffix_size) || suffix_size == 0) {
      return request;
    }
    suffix_size = std::min(suffix_size, file_size);
    start = file_size - suffix_size;
    end = file_size - 1;
  } else {
    if (!parse_uintmax(start_part, start)) {
      return request;
    }
    if (end_part.empty()) {
      end = file_size - 1;
    } else if (!parse_uintmax(end_part, end)) {
      return request;
    }
  }

  if (start >= file_size || end < start) {
    return request;
  }

  request.valid = true;
  request.range = {start, std::min(end, file_size - 1)};
  return request;
}

} // namespace

namespace media {
media_cash::FileCache
    file_cache(Config::giveMe().pages_config.media_cache_size);

std::unordered_map<std::string, std::string> content_types = {
    {".html", "text/html"},
    {".md", "text/html"},
    {".htm", "text/html"},
    {".css", "text/css"},
    {".js", "text/javascript"},
    {".json", "application/json"},
    {".xml", "application/xml"},
    {".xhtml", "application/xhtml+xml"},
    {".txt", "text/plain"},
    {".csv", "text/csv"},
    {".tsv", "text/tab-separated-values"},
    {".ics", "text/calendar"},
    {".rtf", "application/rtf"},
    {".pdf", "application/pdf"},
    {".zip", "application/zip"},
    {".7z", "application/x-7z-compressed"},
    {".rar", "application/x-rar-compressed"},
    {".tar", "application/x-tar"},
    {".gz", "application/gzip"},
    {".bz2", "application/x-bzip2"},
    {".xz", "application/x-xz"},
    {".exe", "application/x-msdownload"},
    {".dmg", "application/x-apple-diskimage"},
    {".iso", "application/x-iso9660-image"},
    {".img", "application/x-iso9660-image"},
    {".jpg", "image/jpeg; charset=utf-8"},
    {".jpeg", "image/jpeg; charset=utf-8"},
    {".png", "image/png; charset=utf-8"},
    {".gif", "image/gif"},
    {".bmp", "image/bmp"},
    {".svg", "image/svg+xml"},
    {".webp", "image/webp"},
    {".ico", "image/vnd.microsoft.icon"},
    {".tif", "image/tiff"},
    {".tiff", "image/tiff"},
    {".mp3", "audio/mpeg"},
    {".mp4", "video/mp4"},
    {".mkv", "video/x-matroska"},
    {".webm", "video/webm"},
    {".mp3", "audio/mpeg"},
    {".mp4", "video/mp4"},
    {".mkv", "video/x-matroska"},
    {".webm", "video/webm"},
};

std::unordered_set<std::string> allowed_no_token = {
    ".png", ".jpg", ".jpeg", ".gif", ".mp4", ".mp3", ".mkv", ".webm", ".svg",
};

std::string save_file(std::string path, std::string file_name,
                      std::string file) {
  std::ofstream out(path + file_name, std::ios::binary);
  out << file;
  out.close();
  return path + file_name;
}

std::string get_file_type(std::string file_name) {
  std::string type = file_name.substr(file_name.find_last_of('.'));
  return type;
}

std::string check_if_file_exists(std::string file_name) {
  std::string full_path =
      Config::giveMe().pages_config.media_path.absolute + file_name;
  std::ifstream file(full_path);
  if (file.good()) {
    return full_path;
  } else {
    return "";
  }
}

std::vector<std::string> get_all_files(std::string path) {
  std::vector<std::string> files;
  for (const auto &entry : std::filesystem::directory_iterator(path)) {
    files.push_back(entry.path());
  }
  return files;
}

bool is_secret(std::string file_name, std::string token,
               std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  (void)token;
  if (file_name.empty()) {
    return false;
  }
  std::string file_name_with_slash = file_name;
  std::string file_name_without_slash = file_name;
  if (file_name[0] == '/') {
    file_name_without_slash = file_name.substr(1);
  } else {
    file_name_with_slash = '/' + file_name;
  }
  cp::SafeCon con{pool_ptr};
  std::vector<std::string> params = {file_name_with_slash, file_name_without_slash};
  pqxx::result res = con->execute_params(
      "SELECT EXISTS ("
      "SELECT 1 FROM \"posts\" p "
      "WHERE p.post_path IN ($1, $2) AND p.is_secret = true "
      "UNION "
      "SELECT 1 FROM \"post_media\" pm "
      "JOIN \"posts\" p ON p.post_id::text = pm.post_id "
      "WHERE pm.media IN ($1, $2) "
      "AND (p.is_secret = true OR COALESCE(pm.is_secret, false) = true)"
      ") AS is_secret;",
      params);


  if (res.empty())
    return false;
  return res[0]["is_secret"].as<bool>();
}
} // namespace media

namespace media::server {
void get_file(std::unique_ptr<restinio::router::express_router_t<>> &router,
              std::shared_ptr<cp::ConnectionsManager> pool_ptr,
              std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
  router.get()->http_get(R"(/media/get/:filename(.*))", [pool_ptr, logger_ptr](
                                                            auto req, auto) {
    logger_ptr->trace([] { return "called /media/get/:filename"; });
    FileStatus status = FileStatus::AVALUABLE;
    std::string token;
    std::string content_type;
    token = security::bearer_or_cookie_token(req->header());
    if (token.empty()) {
      status = FileStatus::UNAUTHORIZED;
    }
    auto user = auth::get_username(token, pool_ptr);
    auto qrl = req->header().path();
    std::string relative_filename =
        url::get_string_after(req->header().path(), "/media/get/");
    std::string file_path = media::check_if_file_exists(relative_filename);

    if (file_path.empty() || file_path == "get" || file_path == "") {
      status = FileStatus::NOT_FOUND;
    } else {
      std::string file_type = media::get_file_type(relative_filename);
      if (allowed_no_token.find(file_type) != allowed_no_token.end()) {
        status = FileStatus::SHARED;
      }
      if (status != FileStatus::SHARED &&
          media::content_types.find(file_type) == media::content_types.end()) {
        status = FileStatus::UNKNOWN_TYPE;
      } else {
        content_type = content_type(file_type);
        if (content_type.empty()) {
          status = FileStatus::UNKNOWN_TYPE;
        }
      }
      if (relative_filename.find("..") != std::string::npos ||
          relative_filename[0] == '/') {
        status = FileStatus::HACKING;
      } else if (media::is_secret(relative_filename, token, pool_ptr)) {
        if (user.empty()) {
          status = FileStatus::UNAUTHORIZED;
        } else if (security::can_read_secret_posts(user, pool_ptr)) {
          status = FileStatus::AVALUABLE;
        } else {
          status = FileStatus::SECRET;
        }
      }
    }
    logger_ptr->info([status, file_path, relative_filename] {
      return fmt::format("relative_filename '{}', file '{}', status '{}'",
                         relative_filename, file_path,
                         static_cast<int>(status));
    });
    switch (status) {
    case FileStatus::AVALUABLE:
    case FileStatus::SHARED:
      break;
    case FileStatus::NOT_FOUND:
      return req->create_response(restinio::status_not_found())
          .append_header("Content-Type", "application/json; charset=utf-8")
          .set_body("empty or wrong file name")
          .done();
    case FileStatus::HACKING:
      return req->create_response(restinio::status_forbidden())
          .append_header("Content-Type", "application/json; charset=utf-8")
          .set_body(Comment::giveMe().no_access)
          .done();
    case FileStatus::SECRET:
      return req
          ->create_response(restinio::status_non_authoritative_information())
          .append_header("Content-Type", "text/html; charset=utf-8")
          .set_body(restinio::sendfile(
              Config::giveMe().pages_config.secret_example_path.absolute))
          .done();
    case FileStatus::UNKNOWN_TYPE:
      return req->create_response(restinio::status_not_found())
          .append_header("Content-Type", "application/json; charset=utf-8")
          .set_body("unknown file type")
          .done();
    case FileStatus::UNAUTHORIZED:
      return req->create_response(restinio::status_unauthorized())
          .append_header("Content-Type", "application/json; charset=utf-8")
          .set_body(Comment::giveMe().no_access)
          .done();
    }

    std::string range_header;
    try {
      range_header = req->header().get_field("Range");
    } catch (const std::exception &) {
    }

    std::error_code size_error;
    std::uintmax_t file_size = std::filesystem::file_size(file_path, size_error);
    if (!size_error) {
      RangeRequest range_request = parse_range_header(range_header, file_size);
      if (range_request.present && !range_request.valid) {
        return req
            ->create_response(restinio::status_requested_range_not_satisfiable())
            .append_header(restinio::http_field::accept_ranges, "bytes")
            .append_header(restinio::http_field::content_range,
                           "bytes */" + std::to_string(file_size))
            .done();
      }
      if (range_request.valid) {
        std::uintmax_t content_length =
            range_request.range.end - range_request.range.start + 1;
        std::ostringstream content_range;
        content_range << "bytes " << range_request.range.start << "-"
                      << range_request.range.end << "/" << file_size;

        return req->create_response(restinio::status_partial_content())
            .append_header(restinio::http_field::content_type, content_type)
            .append_header(restinio::http_field::accept_ranges, "bytes")
            .append_header(restinio::http_field::content_range,
                           content_range.str())
            .append_header("Content-Length", std::to_string(content_length))
            .set_body(restinio::sendfile(file_path).offset_and_size(
                static_cast<restinio::file_offset_t>(range_request.range.start),
                static_cast<restinio::file_size_t>(content_length)))
            .done();
      }
    }

    auto file_content = media::file_cache.get_file(file_path);
    if (file_content == nullptr) {
      return req->create_response()
          .append_header(restinio::http_field::content_type, content_type)
          .append_header(restinio::http_field::accept_ranges, "bytes")
          .set_body(restinio::sendfile(file_path))
          .done();
    } else {
      logger_ptr->info([file_path] {
        return fmt::format("File '{}' found in cache", file_path);
      });
      return req->create_response()
          .append_header(restinio::http_field::content_type, content_type)
          .append_header(restinio::http_field::accept_ranges, "bytes")
          .set_body(std::string(file_content->begin(), file_content->end()))
          .done();
    }
  });
}
} // namespace media::server
