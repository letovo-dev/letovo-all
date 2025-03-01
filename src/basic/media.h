#pragma once
#include <restinio/all.hpp>
#include "pqxx_cp.h"
#include <pqxx/pqxx>
#include <unordered_set>
#include "rapidjson/document.h"
#include "asio/ip/detail/endpoint.hpp"
#include "hash.h"
#include "auth.h"
#include "comment.h"
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <fstream>
#include <filesystem>
#include "config.h"
#include "lodepng.h"

namespace media {
    #define TOP_LEFT_CORNER(side, corner_size) side / corner_size
    #define TOP_RIGHT_CORNER(side, corner_size) side / corner_size - side + 3
    #define BOTTOM_LEFT_CORNER(side, corner_size) side / corner_size - side + 3
    #define BOTTOM_RIGHT_CORNER(side, corner_size) (side * 2 - side / corner_size) - 1
    #define INDEX(i, pixel_size) i * pixel_size

    #define content_type(file_name) content_types[file_name]
    #define media_type(file_name) media_types[file_name]

    extern const int pixel_size;
    extern const int corner_size;
    extern const unsigned char blank;
    

    extern unordered_map<string, string> media_types;

    struct Image {
        std::vector<unsigned char> data;
        unsigned width;
        unsigned height;
        Image(std::vector<unsigned char> data, unsigned width, unsigned height);
    };

    Image decode_image(const char* filename);

    void encode_image(const char* filename, std::vector<unsigned char>& image, unsigned width, unsigned height);

    std::string save_file(std::string path, std::string file_name, std::string file);

    std::string get_file_type(std::string file_name);

    std::string check_if_file_exists(std::string file_name);

    std::vector<std::string> get_all_files(std::string path);

    bool can_i_read(std::string token, std::string file_name, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    void cut_corners(Image& img);

    void cut_media(std::string file_name);
} // namespace media

namespace media::server {
    void get_file(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void post_file(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void get_all_files(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
} // namespace media::server
