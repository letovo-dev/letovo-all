#include "media.h"

namespace media {
    Image::Image(
        std::vector<unsigned char> data,
        unsigned width,
        unsigned height
    )
    : data(data)
    , width(width)
    , height(height) {};

    const int pixel_size = Config::giveMe().media_config.pixel_size;
    const int corner_size = Config::giveMe().media_config.corner_size;
    const unsigned char blank = decode_image(Config::giveMe().media_config.path_to_blank).data[0];

    Image decode_image(const char* filename) {
        std::vector<unsigned char> image;
        unsigned width, height;

        unsigned error = lodepng::decode(image, width, height, filename);

        if(error) std::cout << "decoder error " << error << ": " << lodepng_error_text(error) << std::endl;

        return Image(image, width, height);
    }

    void encode_image(const char* filename, std::vector<unsigned char>& image, unsigned width, unsigned height) {
        unsigned error = lodepng::encode(filename, image, width, height);

        if(error) std::cout << "encoder error " << error << ": "<< lodepng_error_text(error) << std::endl;
    }

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

    std::unordered_map<std::string, std::string> media_types = {
        {"avatar", Config::giveMe().pages_config.user_avatars_path.absolute},
        {"admin_avatar", Config::giveMe().pages_config.admin_avatars_path.absolute},
        {"achivement", Config::giveMe().pages_config.achivements_path.absolute},
        {"media", Config::giveMe().pages_config.media_path.absolute},
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

    void cut_corners(Image& img) {
        int side = std::min((int)img.width, (int)img.height);

        for(int i = 0; i < side * side; i += 1) {
            if (
                false
                || i % side + (int)(i / side) < TOP_LEFT_CORNER(side, corner_size)
                || i % side + (int)(i / side) >= BOTTOM_RIGHT_CORNER(side, corner_size)
                || i % side - (int)(i / side) <= BOTTOM_LEFT_CORNER(side, corner_size)
                || (int)(i / side) - i % side <= TOP_RIGHT_CORNER(side, corner_size)
            ) {
                img.data[INDEX(i, pixel_size)] = blank;
                img.data[INDEX(i, pixel_size) + 1] = blank;
                img.data[INDEX(i, pixel_size) + 2] = blank;
                img.data[INDEX(i, pixel_size) + 3] = blank;
            }
        }
    }

    void cut_media(std::string file_name) {
        Image img = media::decode_image(file_name.c_str());
        media::cut_corners(img);
        media::encode_image(file_name.c_str(), img.data, img.width, img.height);
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

    void post_file(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_post("/media/post", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->info([] { return "post file request"; });
            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());

            std::string token;
            try {
                token = req->header().get_field("Bearer");
            } catch (const std::exception& e) {
                return req->create_response(restinio::status_unauthorized()).done();
            }

            if (token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }

            logger_ptr->info([token] { return fmt::format("token = {}", token); });

            if (!auth::is_admin(token, pool_ptr)) {
                return req->create_response(restinio::status_forbidden())
                .append_header("Content-Type", "text/plain; charset=utf-8")
                .set_body(Comment::giveMe().no_access)
                .done();
            }
            if(!new_body.HasMember("media_type")) {
                return req->create_response(restinio::status_bad_request())
                .append_header("Content-Type", "text/plain; charset=utf-8")
                .set_body("no media type")
                .done();
            };
            if(
                (media_types.find(new_body["media_type"].GetString()) != media_types.end())
            ) {
                auto mt = new_body["media_type"].GetString();
                logger_ptr->info([mt] { return fmt::format("media type = {}", mt); });
                return req->create_response(restinio::status_bad_request())
                .append_header("Content-Type", "text/plain; charset=utf-8")
                .set_body("wrong media type")
                .done();
            }
            

            std::string username = auth::get_username(token, pool_ptr);

            if (username.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }

            logger_ptr->info([username] { return fmt::format("username = {}", username); });

            if (new_body.HasMember("file") && new_body.HasMember("file_name")) {
                std::string full_path;
                auto file = new_body["file"].GetString(); 
                logger_ptr->info([file] { return fmt::format("file = {}", file[10]); });
                try {
                    full_path = media::save_file(
                        media_type(new_body["media_type"].GetString()),
                        new_body["file_name"].GetString(),
                        new_body["file"].GetString()
                    );
                } catch (...) {
                    return req->create_response(restinio::status_bad_request())
                    .append_header("Content-Type", "text/plain; charset=utf-8")
                    .set_body("can't save file")
                    .done();
                };
                if (new_body["media_type"].GetString() == "avatar") {
                    media::cut_media(full_path);                
                }
                return req->create_response()
                    .append_header("Content-Type", "application/json; charset=utf-8")
                    .set_body("{\"result\": \"file saved\"}")
                    .done();
            } else {
                return req->create_response(restinio::status_bad_request())
                .append_header("Content-Type", "text/plain; charset=utf-8")
                .set_body("no file or file name")
                .done();
            }
        });
    }
} // namespace media::server
