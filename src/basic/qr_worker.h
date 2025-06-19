#pragma once

#include <qrencode.h>
#include <png.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <chrono>
#include <vector>

#include <restinio/all.hpp>
#include "rapidjson/document.h"
#include "pqxx_cp.h"
#include "auth.h"

namespace qr {
    const int SCALE = 12;
    const int MARGIN = 2;

    bool SavePng(const char* filename, int width, int height, const std::vector<png_byte>& image);

    void FillInVec(std::vector<png_byte>& image, int img_size, int qr_size, const QRcode* q);

    bool GenerateQRCode(const char* data, const char* filename);
}

namespace qr::server {
    void achivement_qr(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void page_qr(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
}