#include <cassert>
#include <chrono>
#include <curl/curl.h>
#include <iostream>
#include <thread>
#include <vector>

const std::string USERNAME = "scv-8";
const std::string PASSWORD = "8";
const std::string BASE_URL = "http://localhost/api/auth/login";

size_t discardResponse(void *ptr, size_t size, size_t nmemb, void *userdata) {

  return size * nmemb;
}

void performLogin(int amount = 10) {
  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < amount; ++i) {
    CURL *curl = curl_easy_init();
    if (curl) {

      curl_easy_setopt(curl, CURLOPT_URL, BASE_URL.c_str());

      std::string postData = "{\"login\": \"" + USERNAME +
                             "\", \"password\": \"" + PASSWORD + "\"}";
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());

      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discardResponse);

      CURLcode res = curl_easy_perform(curl);
      if (res != CURLE_OK) {
        std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res)
                  << std::endl;
      } else {

        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        // Accept any valid HTTP response (200-299 success, or 400-499 client errors)
        // Only fail if server error (500+) or no response
        if (response_code < 200 || response_code >= 500) {
          std::cerr << "Unexpected response code: " << response_code << std::endl;
        }
      }

      curl_easy_cleanup(curl);
    }
  }

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = end - start;
  std::cout << (elapsed.count() / amount) << " average seconds" << std::endl;
}

void testLoad(int threads = 100) {
  std::vector<std::thread> threadsList;

  for (int i = 0; i < threads; ++i) {

    threadsList.emplace_back(performLogin, 10);
  }

  for (auto &thread : threadsList) {
    thread.join();
  }
}

int main() {

  curl_global_init(CURL_GLOBAL_ALL);

  testLoad();

  curl_global_cleanup();

  return 0;
}