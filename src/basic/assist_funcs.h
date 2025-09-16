#pragma once

#include <any>
#include <boost/format.hpp>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <rapidjson/document.h>
#include <restinio/all.hpp>
#include <unordered_map>

namespace assist {
std::unordered_map<std::string, std::any>
parse_page_content(rapidjson::Document &new_body);

void add_id_to_page(
    const std::string &filePath, int number,
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

void create_file(const std::string &filePath, const std::string &content,
                 std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

void create_mdx_from_template_file(
    const std::string &filePath, const std::string &title,
    const std::string &post_id,
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

void create_mdx_from_template_file(
    const std::string &filePath, const std::string &title,
    const std::string &author, const std::string &post_id,
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

void fix_new_lines(std::string &content);
} // namespace assist
