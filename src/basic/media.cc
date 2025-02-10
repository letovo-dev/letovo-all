#include "media.h"

namespace media {
    std::unordered_map<std::string, std::string> content_types = {
        {".html", "text/html"},
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

    std::string save_file(std::string path, std::string file_name, std::string file) {
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
        std::string full_path = Config::giveMe().pages_config.media_path.absolute + file_name;
        std::ifstream file(full_path);
        if (file.good()) {
            return full_path;
        } else {
            return "";
        }
    }

    std::vector<std::string> get_all_files(std::string path) {
        std::vector<std::string> files;
        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            files.push_back(entry.path());
        }
        return files;
    }

    // not tested
    bool can_i_read(std::string token, std::string file_name, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());

        std::vector<std::string> params = {hashing::string_from_hash(token), file_name};

        pqxx::result result = con->execute_params("SELECT * FROM \"user\" left join \"file_rights\" on \"user\".role = \"file_rights\" where \"user\".username = ($1) and \"file_rights\".file_path = ($2);", params);

        if (result.empty()) {
            return false;
        }
        return true;
    }
} // namespace media

namespace media::server {
    void get_file(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/media/get/:filename(.*))", [pool_ptr, logger_ptr](auto req, auto) {
            auto qrl = req->header().path();
            std::string relative_filename = url::get_string_after(req->header().path(), "/media/get/");
            std::string file_path = media::check_if_file_exists(relative_filename);

            if (file_path.empty() || file_path == "get") {
                return req->create_response(restinio::status_not_found())
                .append_header("Content-Type", "application/json; charset=utf-8")
                .set_body("empty or wrong file name")
                .done();
            }

            if (relative_filename.find("..") != std::string::npos || relative_filename[0] == '/') {
                return req->create_response(restinio::status_forbidden())
                .append_header("Content-Type", "application/json; charset=utf-8")
                .set_body(Comment::giveMe().no_access)
                .done();
            }

            std::string file_type = media::get_file_type(relative_filename);
            if (media::content_types.find(file_type) == media::content_types.end()) {
                return req->create_response(restinio::status_not_found())
                .append_header("Content-Type", "application/json; charset=utf-8")
                .set_body("uncnown file type")
                .done();
            }
            std::string content_type = content_type(file_type);
            logger_ptr->info([file_path] { return fmt::format("requested file {}", file_path); });
            if (content_type.empty()) {
                return req->create_response(restinio::status_not_found()).done();
            }
            return req->create_response()
                .append_header(restinio::http_field::content_type, content_type)
                .set_body(restinio::sendfile(file_path))
                .done();
        });
    }
} // namespace media::server
