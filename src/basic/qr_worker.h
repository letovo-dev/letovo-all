#pragma once

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <png.h>
#include <qrencode.h>
#include <vector>

#include "auth.h"
#include "pqxx_cp.h"
#include "rapidjson/document.h"
#include <restinio/all.hpp>

namespace qr {
const int SCALE = 12;
const int MARGIN = 2;

bool SavePng(const char *filename, int width, int height,
             const std::vector<png_byte> &image);

void FillInVec(std::vector<png_byte> &image, int img_size, int qr_size,
               const QRcode *q);

bool GenerateQRCode(const char *data, const char *filename);
} // namespace qr

namespace qr::server {
void achivement_qr(
    std::unique_ptr<restinio::router::express_router_t<>> &router,
    std::shared_ptr<cp::ConnectionsManager> pool_ptr,
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

void page_qr(std::unique_ptr<restinio::router::express_router_t<>> &router,
             std::shared_ptr<cp::ConnectionsManager> pool_ptr,
             std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
} // namespace qr::server
