#include "qr_worker.h"

namespace qr {
    bool SavePng(const char* filename, int width, int height, const std::vector<png_byte>& image) {
        FILE *fp = fopen(filename, "wb");
        if (!fp) {
            std::cerr << "can't open file: " << filename << "\n";
            return false;
        }
        png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
        if (!png_ptr) { fclose(fp); return false; }
        png_infop info_ptr = png_create_info_struct(png_ptr);
        if (!info_ptr) { png_destroy_write_struct(&png_ptr, nullptr); fclose(fp); return false; }
        if (setjmp(png_jmpbuf(png_ptr))) {
            png_destroy_write_struct(&png_ptr, &info_ptr);
            fclose(fp);
            return false;
        }
        png_init_io(png_ptr, fp);
        // RGBA
        png_set_IHDR(png_ptr, info_ptr, width, height,
                     8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                     PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
        png_write_info(png_ptr, info_ptr);
    
        std::vector<png_bytep> rows(height);
        for (int y = 0; y < height; ++y) {
            rows[y] = (png_bytep)&image[y * width * 3];
        }
        png_write_image(png_ptr, rows.data());
        png_write_end(png_ptr, nullptr);
    
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        return true;
    }

    void FillInVec(std::vector<png_byte>& image, int img_size, int qr_size, const QRcode* q) {
        for (int y = 0; y < qr_size; ++y) {
            for (int x = 0; x < qr_size; ++x) {
                bool module = q->data[y * qr_size + x] & 1;
                png_byte color = module ? 0x00 : 0xFF;
                int x0 = (MARGIN + x) * SCALE;
                int y0 = (MARGIN + y) * SCALE;
                for (int dy = 0; dy < SCALE; ++dy) {
                    for (int dx = 0; dx < SCALE; ++dx) {
                        int px = x0 + dx;
                        int py = y0 + dy;
                        int idx = (py * img_size + px) * 3;
                        image[idx + 0] = color;
                        image[idx + 1] = color;
                        image[idx + 2] = color;
                    }
                }
            }
        }    
    }

    bool GenerateQRCode(const char* data, const char* filename) {
        QRcode *q = QRcode_encodeString(data, 0, QR_ECLEVEL_L, QR_MODE_8, 1);
        if (!q) {
            std::cerr << "qr gen error\n";
            return false;
        }

        int qr_size = q->width;
        int img_size = (qr_size + 2 * MARGIN) * SCALE;
        std::vector<png_byte> image(img_size * img_size * 3, 0xFF); // белый фон RGB

        FillInVec(image, img_size, qr_size, q);
        SavePng(filename, img_size, img_size, image);

        QRcode_free(q);
        return true;
    }

    inline bool file_exists(const std::string& name) {
        struct stat buffer;   
        return (stat (name.c_str(), &buffer) == 0); 
      }
      
}

namespace qr::server {
    void achivement_qr(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/achivements/qr/:achivement_id([0-9]+))", [pool_ptr, logger_ptr](auto req, auto params) {
            std::string token;
            auto qrl = req->header().path();

            std::string achivement_id = url::get_last_url_arg(qrl);
            try {
                token = req -> header().get_field("Bearer");
            } catch (const std::exception& e) {
                logger_ptr->info([]{return "can't get token";});
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if(achivement_id == "qr" || achivement_id.empty()) {
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }
            std::string username = auth::get_username(token, pool_ptr);
            std::string data = fmt::format("http://{}/ach_getter/{}/{}", Config::giveMe().server_config.adress, achivement_id, username);
            std::string filename_str = fmt::format("achivmenet_{}_{}.png", achivement_id, username);
            if(!qr::file_exists(filename_str)) {
                const char* filename = filename_str.c_str();
                qr::GenerateQRCode(data.c_str(), filename);
            }
            
            return req->create_response()
                .append_header(restinio::http_field::content_type, "image/png")
                .set_body(restinio::sendfile(filename_str))
                .done();
            
        });
    }

    void page_qr(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/post/qr/:post_id([0-9]+))", [pool_ptr, logger_ptr](auto req, auto params) {
            std::string token;
            auto qrl = req->header().path();

            std::string post_id = url::get_last_url_arg(qrl);
            try {
                token = req -> header().get_field("Bearer");
            } catch (const std::exception& e) {
                logger_ptr->info([]{return "can't get token";});
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if(!auth::is_admin(token, pool_ptr)) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if(post_id == "qr" || post_id.empty()) {
                return req->create_response(restinio::status_non_authoritative_information()).done();
            }
            std::string data = fmt::format("http://{}/api/post/reveal_secret/{}", Config::giveMe().server_config.adress, post_id);
            std::string filename_str = fmt::format("post_{}.png", post_id);
            if(!qr::file_exists(filename_str)) {
                const char* filename = filename_str.c_str();
                qr::GenerateQRCode(data.c_str(), filename);
            }

            return req->create_response()
                .append_header(restinio::http_field::content_type, "image/png")
                .set_body(restinio::sendfile(filename_str))
                .done();
        });
    }
}