#pragma once

#include <qrencode.h>
#include <png.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace qr {
    const int SCALE = 12;
    const int MARGIN = 2;

    bool SavePng(const char* filename, int width, int height, const std::vector<png_byte>& image);

    void FillInVec(std::vector<png_byte>& image, int img_size, int qr_size, const QRcode* q);

    bool GenerateQRCode(const char* data, const char* filename);
}